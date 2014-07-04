/*
	Copyright (C) 2003-2013 by David White <davewx7@gmail.com>
	
    This program is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 2 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/
#include <boost/bind.hpp>

#include "button.hpp"
#include "controls_dialog.hpp"
#include "controller_select_dialog.hpp"
#include "dialog.hpp"
#include "draw_scene.hpp"
#include "dropdown_widget.hpp"
#include "graphical_font_label.hpp"
#include "i18n.hpp"
#include "image_widget.hpp"
#include "joystick.hpp"
#include "joystick_configure_dialog.hpp"
#include "json_parser.hpp"
#include "level.hpp"
#include "module.hpp"
#include "pause_game_dialog.hpp"
#include "preferences.hpp"
#include "sound.hpp"
#include "language_dialog.hpp"
#include "video_selections.hpp"
#include "widget_factory.hpp"

namespace {
    
        // The Joystick Configuration Dialog works by asking the player to
        // press the relevant button/hat/axis for each in-game control signal,
        // up, down, left etc.
        //
        // We take this interrogative approach because it is a low common
        // denominator that is relatively quick and painless for the player.  
        // It means the player doesn't need to fiddle round indefinitely trying
        // to work out whether the button called triangle on her controller is
        // what the Linux driver calls button 3.
        // 
        // The dialog is roughly laid out:
        //
        // [Title label: "Configure Controller"]
        //
        // [Finger image]  [Instruction label: "Please press JUMP now."]
        // 
        // [Back button]
        //
        // [Cancel button] [Okay button]
        // 
        // At the end the user presses Okay to save the configuration or 
        // cancel to abandon it.  The finger image flashes next to the 
        // instruction label to attract attention when the user needs to press
        // the next button. 

    
    void end_dialog(gui::dialog* d);

    class JoystickMonitoringLabel : public gui::graphical_font_label {
        // JoystickMonitoringLabel is a graphical_font_label that also
        // manages the configuration process for us - it works as a kind of
        // state machine. 
        //
        // Every gui update cycle it prompts the user or uses the joystick::
        // configuration functions to listen for controller input.
        //
        // In addition to telling the player what to press when, the
        // JoystickMonitoringLabel also decides when the Previous and Okay
        // buttons should be active, and when the finger image needs to flash.
       
        private:

            // Our states.
            enum State {
                welcome,                // Brief pause at start
                neutral_zone_start,     // Saying "Please leave your controller still and press Okay to start"
                getting_neutral_zone,   // Saying "Please leave the controller still now"
                lively_neutral_zone,    // Saying "Your neutral zone is lively."
                getting_button,         // Saying "Press the button for JUMP" 
                already_used,           // Telling user they used same button twice.
                confirming_got_button,  // Telling user their button press was received.
                decide_finished,        // Checking whether any more buttons to go (instantaneous)
                finished,               // Telling the user they can press Okay now.
                aborted                 // Do nothing, have been cancelled
            };

            // Both our ticks count down the number of gui refresh cycles.  
            // 
            // When 'tick' is above zero, it will count down till it hits zero.  
            // Then it will transition with next_state().
            // If 'tick' is set to zero, it will never automtically transition.
            //
            // 'finger_tick' counts down from 10 to 1 then cycles back to 10 etc.
            // It is only used in the getting_button state.
            int tick;
            int finger_tick;
            
            int curr_control;           // Which in-game control we are up to.
            State state;                // The current state.  Must only be changed by calling start_new_state().
            joystick::listen_result result; // Result of trying to fetch a controller press from the user.
            bool do_neutral_check; // Whether we ask the player to set the neutral positions of the axes.

            // Other gui components we need to affect
            gui::button* okay_button;
            gui::graphical_font_label* okay_label;
            gui::button* cancel_button;
            gui::image_widget* finger_image;
            gui::button* previous_button;
            gui::graphical_font_label* previous_label;

        public:

            // After construction, the JoystickMonitoringLabel is ready to run
            // as soon as it is added to a dialog and that dialog is run.
            JoystickMonitoringLabel(
                    const std::string& text, const std::string& font, int size,
                    gui::button* okay_button_in, gui::graphical_font_label* okay_label_in, 
                    gui::button* cancel_in, gui::image_widget* finger_in, 
                    gui::button* previous_button_in, gui::graphical_font_label* previous_label_in) 
            : graphical_font_label(text, font, size) {
                okay_button = okay_button_in;
                okay_label = okay_label_in;
                cancel_button = cancel_in;
                previous_button = previous_button_in;
                previous_label = previous_label_in;
                finger_image = finger_in;
                tick = 0;
                finger_tick = 0;
                do_neutral_check = false;
                start_new_state(welcome);
            }

            // effect_remap() does:
            // 1) Actually use the mapping we have created
            // 2) Set preferences to use this configuration
            // 3) Shut down joystick:: module configuration 
            void effect_remap() {
                if(state != finished) {
                    std::cerr << "cannot effect remap unless state is finished" << std::endl;
                    return;
                }
                joystick::apply_configuration();
                joystick::stop_configurer();
                joystick::set_joystick_configuration_preferences();
            }

            // Backtracks to the previous in-game control (in case you make a mistake mid configuration), or even to the
            // neutral-zone analysis step.
            void back_one_control() {
                // disable okay, finger
                if(curr_control != 0) {
                    curr_control--;
                    joystick::retreat();
                    start_new_state(getting_button);
                } else if(curr_control == 0 && do_neutral_check) {
                    start_new_state(neutral_zone_start);
                }
            } 

            // When the okay button is clicked:
            // 1) Actually use the mapping the user has just created for the in-game joystick, and
            // 2) store it in preferences::.
            void okay_button_click(gui::dialog* d) {
                if(state == finished) {
                    effect_remap();
                    end_dialog(d);
                } else if(state == neutral_zone_start) {
                    start_new_state(getting_neutral_zone);
                } else if(state == lively_neutral_zone) {
                    start_new_state(getting_button);
                }
            }

            // When the cancel button is clicked:
            // 1) Go into the abort state
            // 2) Clean up joystick:: configurer
            // 3) Kill the dialog
            void cancel_button_click(gui::dialog* d) {
                start_new_state(aborted);
                joystick::stop_configurer();
                end_dialog(d);
            }

        protected:

            // next_state is called when we tick down from one to zero to decide what the next state should be.  Not all states
            // actually utilise this function - for the states that stay permanently on tick zero it is never called.
            enum State next_state(State state) {
                switch(state) {
                    case welcome:
                        if(do_neutral_check) {
                            return neutral_zone_start;
                        } else {
                            return getting_button;
                        }
                    case neutral_zone_start:
                        return getting_neutral_zone;
                    case getting_neutral_zone:
                        if(joystick::neutral_zones_dangerous()) {
                            return lively_neutral_zone;
                        } else {
                            return getting_button;
                        }
                    case lively_neutral_zone:
                        return getting_button;
                    case getting_button:
                        return confirming_got_button;
                    case already_used:
                        return getting_button;
                    case confirming_got_button:
                        return decide_finished;
                    case decide_finished:
                        return finished;
                    case finished:
                        return finished;
                    case aborted:
                        return aborted;
                    default:
                        ASSERT_FATAL("state out of range");
                }
            }

            // Changes the current state to new_state and performs the (re-)initialisation necessary for new_state.
            // Note that certain states are instantaneous, in that their logic in this function will perform some
            // action and then finish with recursive call so we can start_new_state() on another state.
            void start_new_state(State new_state) {
                state = new_state;
                switch(state) {
                    case welcome:
                        tick = 30;
                        curr_control = 0;
                        joystick::start_configurer();
                        do_neutral_check = !joystick::neutral_zones_known();
                        set_text("\n \nStarting...");
                        okay_button->enable(true); // XXX widget.hpp coding bug: enable(true) is actually disable(true).
                        okay_label->enable(true); 
                        previous_button->enable(true);
                        previous_label->enable(true);
                        finger_image->set_visible(false);
                        break;
                    case neutral_zone_start:
                        tick = 0;
                        okay_button->enable(false);
                        okay_label->enable(false);
                        previous_button->enable(true);
                        previous_label->enable(true);
                        finger_image->set_visible(false);
                        joystick::clear_neutral_zones();
                        set_text(   "First we need to calibrate the neutral\n"
                                    "positions on your controller.\n"
                                    " \n"
                                    "Please press NOTHING on your controller,\n"
                                    "and select Okay to start.");
                        break;
                    case getting_neutral_zone:
                        tick = 120;
                        finger_tick = 10;
                        okay_button->enable(true); 
                        okay_label->enable(true);
                        previous_button->enable(true);
                        previous_label->enable(true);
                        finger_image->set_visible(true);
                        set_text(   " \n"
                                    "Checking device...\n"
                                    " \n"
                                    "Please press NOTHING.\n");
                        break;
                    case lively_neutral_zone:
                        tick = 0;
                        okay_button->enable(false);
                        okay_label->enable(false);
                        previous_button->enable(false);
                        previous_label->enable(false);
                        finger_image->set_visible(false);
                        set_text(   "Your controller seems to be wobbling.\n"
                                    "Did you press something by mistake?\n"
                                    " \n"
                                    "Select Back to try again.\n"
                                    "Select Okay to keep going anyway."); 
                        break;
                    case getting_button:
                        tick = 0;
                        finger_tick = 10;
                        okay_button->enable(true); 
                        okay_label->enable(true);
                        if(curr_control > 0 || do_neutral_check) {
                            previous_button->enable(false);
                            previous_label->enable(false);
                        } else {
                            previous_button->enable(true);
                            previous_label->enable(true);
                        }
                        set_text(   " \n"
                                    " \n"
                                    "Please press [" + std::string(controls::control_names()[curr_control]) + "] now.");
                        break;
                    case already_used:
                        tick = 60;
                        set_text(   " \n"
                                    " \n"
                                    "You have already used that action.");
                        break;
                    case confirming_got_button:
                        tick = 30;
                        set_text(   " \n"
                                    " \nGot action for [" + std::string(controls::control_names()[curr_control]) + "].");
                        break;
                    case decide_finished:
                        curr_control++;
                        if(curr_control == controls::NUM_CONTROLS) {
                            start_new_state(finished);
                        } else {
                            start_new_state(getting_button);
                        }
                        break;
                    case finished:
                        tick = 0;
                        okay_button->enable(false);
                        okay_label->enable(false);
                        set_text(   "All done!\n"
                                    " \n"
                                    "Press Okay to save.\n"
                                    " \n"
                                    "This will overwrite any old saved configuration.");
                        break;
                    case aborted:
                        break;
                }
            }

            // Called every gui update cycle.  Implements the ticking of our local clock.
            virtual void handle_process() override {
                gui::graphical_font_label::handle_process();
             
                // If 'tick' is above zero, it is counting down every update
                // cycle, and when it hits zero it sparks a transition to the
                // next state.  The getting_neutral_zone state listens to the
                // joystick every tick for a fixed time period.
                if(tick > 0) {
                    tick--;
                    if(tick == 0) {
                        start_new_state(next_state(state));
                    }
            
                    if(state == getting_neutral_zone) {
                        finger_tick--;
                        if(finger_tick <= 0) {
                            finger_tick = 10;
                            finger_image->set_visible(!finger_image->visible());
                        }
                        // The state getting_neutral_zone lasts some time, but we
                        // only check the dead zone towards the end.  This
                        // gives the user a few extra ticks to release all the
                        // buttons. 
                        if(tick < 60) {
                            joystick::examine_neutral_zones_tick();
                        }
                    }
                } else {
                    // If 'tick' is zero, the state lasts indefinitely until
                    // some other condition changes it.  The getting_button
                    // state flashes the finger and listens to the joystick
                    // indefinitely
                    if(state == getting_button) {
                        finger_tick--;
                        if(finger_tick <= 0) {
                            finger_tick = 10;
                            finger_image->set_visible(!finger_image->visible());
                        }

                        joystick::listen_result res = joystick::listen_for_signal();
                        if(res == joystick::duplicate) {
                            start_new_state(already_used);
                        } else if(res == joystick::success_keep_going || res == joystick::success_finished) {
                            start_new_state(confirming_got_button);
                        }
                    }
                }
            }
    };


    gui::widget_ptr title_label;            // "Configuring Game Controller"
    gui::widget_ptr instruction_label;      // "Press UP now"
    gui::widget_ptr previous_label;         // "Redo Previous"
    gui::widget_ptr cancel_label;           // "Cancel"
    gui::widget_ptr okay_label;             // "Okay"


    // Go back and change the previous control signal instead.
    void back_one_control(JoystickMonitoringLabel* jml) {
        jml->back_one_control();
    }

    // Always called when the dialog closes.
    void cleanup(bool ignored) {
        joystick::stop_configurer();
        joystick::set_silent(false);
        std::cerr << "CLEANING UP" << std::endl;
    }

    // Close the dialog.
    void end_dialog(gui::dialog* d) {
        d->close();
    }

    void okay_button_click(gui::dialog* d) {
        static_cast<JoystickMonitoringLabel*>(instruction_label.get())->okay_button_click(d);
    } 
    
    void cancel_button_click(gui::dialog* d) {
        static_cast<JoystickMonitoringLabel*>(instruction_label.get())->cancel_button_click(d);
    } 

    void do_draw_scene() {
        draw_scene(level::current(), last_draw_position());
    }

}

// Create and run the dialog
void show_joystick_configure_dialog()
{
	
	int button_width = 240;//232;
	int button_height = 45;//50;
    //int label_height = 45;
    int text_line_height = 18;
    int outer_padding = 20;
	int padding = 12;//16;
	gui::BUTTON_RESOLUTION button_resolution = gui::BUTTON_SIZE_DOUBLE_RESOLUTION;
	bool upscale_dialog_frame = true;
    const int disabled_opacity = 32;
	
#if TARGET_OS_IPHONE || TARGET_IPHONE_SIMULATOR
	show_return = false;
	show_select = false;
	show_configure = true;
#elif TARGET_BLACKBERRY
	show_return = false;
	show_select = false;
	show_configure = true;
#endif

	using namespace gui;

    // Stop listening to the joystick.
    joystick::set_silent(true);

    // Create labels
    title_label = widget_ptr(new graphical_font_label(_("Configuring Game Controller"), "door_label", 2));
    graphical_font_label* previous_label_raw = new graphical_font_label(_("Back"), "door_label", 2);
    previous_label = widget_ptr(previous_label_raw);
    cancel_label = widget_ptr(new graphical_font_label(_("Cancel"), "door_label", 2));
    graphical_font_label* okay_label_raw = new graphical_font_label(_("Okay"), "door_label", 2);
    okay_label = widget_ptr(okay_label_raw);
    
	ASSERT_LOG(title_label != NULL, "Couldn't create title label widget.");
	ASSERT_LOG(previous_label != NULL, "Couldn't create previous label widget.");
	ASSERT_LOG(cancel_label != NULL, "Couldn't create cancel label widget.");
	ASSERT_LOG(okay_label != NULL, "Couldn't create okay label widget.");


    // Create dialog itself
	int window_w, window_h;

    // Layout:                 Cancel button            Okay button
    window_w = outer_padding + button_width + padding + button_width + outer_padding;

    
    window_h =      outer_padding           //
                +   text_line_height        // Configuration
                +   padding                 //
                +   text_line_height        //       "Push UP now
                +   text_line_height        //        + 2nd line of text
                +   text_line_height        // [pic]  + 3rd line of text
                +   text_line_height        //        + 4th line of text
                +   text_line_height        //        + 5th line of text..."
                +   padding                 //
                +   button_height           // [       BACK       ]
                +   padding                 //
                +   button_height           // [CANCEL]    [ OKAY ]
                +   outer_padding           //
                ;

	dialog d((preferences::virtual_screen_width()/2 - window_w/2) & ~1, (preferences::virtual_screen_height()/2 - window_h/2) & ~1, window_w, window_h);
    d.set_on_close(boost::bind(cleanup, false));
	d.set_padding(padding);
	d.set_background_frame("empty_window");
	d.set_upscale_frame(upscale_dialog_frame);
	d.set_draw_background_fn(do_draw_scene);

    // Create okay and cancel buttons
    button* okay_button_raw = new button(okay_label, boost::bind(okay_button_click, &d), BUTTON_STYLE_NORMAL, button_resolution);
	widget_ptr okay_button(okay_button_raw); 
	okay_button->set_dim(button_width, button_height);
    okay_button->set_disabled_opacity(disabled_opacity);
    okay_label->set_disabled_opacity(disabled_opacity);
    
    button* cancel_button_raw = new button(cancel_label, boost::bind(cancel_button_click, &d), BUTTON_STYLE_NORMAL, button_resolution);
	widget_ptr cancel_button(cancel_button_raw);
	cancel_button->set_dim(button_width, button_height);
    
	button* previous_button_raw = new button(previous_label, NULL, BUTTON_STYLE_NORMAL, button_resolution);
	widget_ptr previous_button(previous_button_raw);
	previous_button->set_dim(2*button_width + padding, button_height);
    previous_button->set_disabled_opacity(disabled_opacity);
    previous_label->set_disabled_opacity(disabled_opacity);

    // Create the blinking finger image
    image_widget* finger_raw = new image_widget("gui/cursor.png");
    widget_ptr finger(finger_raw);

    // Create the instruction label
    JoystickMonitoringLabel* jml_raw = new JoystickMonitoringLabel("-placeholder-text-", "door_label", 2, 
            okay_button_raw, okay_label_raw, cancel_button_raw, finger_raw, previous_button_raw, previous_label_raw);
    instruction_label = widget_ptr(jml_raw);
    instruction_label->set_dim(button_width + padding + button_width, 5*text_line_height);
	ASSERT_LOG(instruction_label != NULL, "Couldn't create instruction label widget.");

    // Link previous_button to instruction_label
    previous_button_raw->set_click_handler(boost::bind(back_one_control, jml_raw));

    // Place widgets in dialog    
    d.set_padding(padding);
    d.add_widget(title_label, outer_padding, outer_padding);
    d.add_widget(finger, outer_padding, outer_padding + text_line_height + padding + (text_line_height*5.0/2.0) - (0.5*finger_raw->height()));
    d.add_widget(instruction_label, outer_padding + finger_raw->width() + padding, outer_padding + text_line_height + padding);
    d.add_widget(previous_button, outer_padding, outer_padding + title_label->height() + padding + instruction_label->height() + padding);
    d.add_widget(cancel_button, dialog::MOVE_RIGHT);
    d.add_widget(okay_button);


    d.show_modal();

	return;
}
