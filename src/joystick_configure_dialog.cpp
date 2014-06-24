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
        // to work out whether button called triangle on her controller is what
        // the Linux driver calls button 3.
        // 
        // The dialog is roughly laid out:
        //
        // [Title label: "Configure Controller"]
        //
        // [Finger image]  [Instruction label: "Please press JUMP now."]
        // 
        // [Previous button: "Go back one control"]
        //
        // [Cancel button] [Okay button]
        // 
        // At the end the user presses Okay to save the configuration or 
        // cancel to abandon it.  The finger image flashes next to the 
        // instruction label to attract attention when the user needs to press
        // the next button. 


    class JoystickMonitoringLabel : public gui::graphical_font_label {
        // JoystickMonitoringLabel is a graphical_font_label that also listens
        // to the joystick, grabs control signals and tells the player what to
        // do when.  It implements the bulk of the logic for the configuration
        // procedure using a slightly dodgy sequence of states.
        //
        // In addition to telling the player what to press when, the
        // JoystickMonitoringLabel also decides when the Previous and Okay
        // buttons should be active, and when the finger image needs to flash.
        //
        // During the configuration procedure it maintains its own record of 
        // which axes/buttons/hats have been pressed corresponding to which
        // in-game control (up/left/jump/tongue etc), assembled by directly
        // listening to the relevant SDL events.  
       
        private:

            // Our states.
            enum State {
                welcome,                // Brief pause at start
                getting_button,         // Saying "Press the button for JUMP" 
                checking_button,        // Checking same button not used twice (instantaneous)
                already_used,           // Telling user they used same button twice.
                confirming_got_button,  // Telling user their button press was received.
                decide_finished,        // Checking whether any more buttons to go (instantaneous)
                finished                // Telling the user they can press Okay now.
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
            State state;                // The current state.  Must only be changed by calling start_state().

            SDL_JoystickID target_device;

            // See joystick.cpp's documentation for how preferences are stored. 
            int part_kinds[controls::NUM_CONTROLS];
            int part_ids[controls::NUM_CONTROLS];
            int part_data0[controls::NUM_CONTROLS];
            int part_data1[controls::NUM_CONTROLS];
            
            // Other gui components we need to affect
            gui::button* okay_button;
            gui::graphical_font_label* okay_label;
            gui::button* cancel_button;
            gui::image_widget* finger_image;
            gui::button* previous_button;
            gui::graphical_font_label* previous_label;

        public:

                // After construction, the JoystickMonitoringLabel is ready to
                // run as soon as it is added to a dialog and that dialog is
                // run.
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
                start_new_state(welcome);
                target_device = joystick::current_device_id();
            }

                // Sends the controller mapping we have accumulated to the joystick:: module. 
            void effect_remap() {
                if(state != finished) {
                    std::cerr << "cannot effect remap unless state is finished" << std::endl;
                    return;
                }
                joystick::change_mapping(part_kinds, part_ids, part_data0, part_data1);
                joystick::set_joystick_configuration_preferences();
            }

                // Backtracks to the previous in-game control (in case you make
                // a mistake mid configuration).
            void back_one_control() {
                if(curr_control != 0) {
                   curr_control--;
                }
                start_new_state(getting_button);
            } 

        protected:

            // next_state is called when we tick down from one to zero to decide what the next state should be.  Not all states
            // actually utilise this function - for the states that stay permanently on tick zero it is never called.
            enum State next_state(State state) {
                switch(state) {
                    case welcome:
                        return getting_button;
                    case getting_button:
                        return checking_button;
                    case checking_button:
                        return confirming_got_button;
                    case already_used:
                        return getting_button;
                    case confirming_got_button:
                        return decide_finished;
                    case decide_finished:
                        return finished;
                    case finished:
                        return finished;
                    default:
                        ASSERT_FATAL("state out of range");
                }
            }

            // handle_event(event, claimed) is how we read in joystick commands
            // from the user.  We only actively harvest events during the
            // getting_button state.  If this SDL 'event' is from the device
            // the user is configuring, we claim it by returning true.
            // Otherwise we return 'claimed' unchanged.
            //
            // The controller action in each captured event is transcribed to
            // the part_* arrays for later despatch to the joystick module. 
            virtual bool handle_event(const SDL_Event& event, bool claimed)
                override {
                if(state != getting_button) {
                    return claimed;
                }    
                switch(event.type) {
                    case SDL_CONTROLLERAXISMOTION:
                    case SDL_JOYAXISMOTION:
                        if(event.jaxis.which == target_device && abs(event.jaxis.value) >= joystick::small_mag) {
                            part_kinds[curr_control] = joystick::AXIS;
                            part_ids[curr_control] = event.jaxis.axis;
                            part_data0[curr_control] = (event.jaxis.value > 0) ? joystick::small_mag : -joystick::large_mag;
                            part_data1[curr_control] = (event.jaxis.value > 0) ? joystick::large_mag : -joystick::small_mag;
                            start_new_state(checking_button);
                            claimed = true;
                        }
                        break;
                    case SDL_CONTROLLERBUTTONDOWN:
                    case SDL_JOYBUTTONDOWN:
                        if(event.jbutton.which == target_device) {
                            part_kinds[curr_control] = joystick::BUTTON;
                            part_ids[curr_control] = event.jbutton.button;
                            part_data0[curr_control] = 0;
                            part_data1[curr_control] = 0;
                            start_new_state(checking_button);
                            claimed = true;
                        }
                        break;
                    case SDL_JOYHATMOTION:
                        if(event.jhat.which == target_device && event.jhat.value != SDL_HAT_CENTERED) {
                            part_kinds[curr_control] = joystick::HAT;
                            part_ids[curr_control] = event.jhat.hat;
                            part_data0[curr_control] = event.jhat.value;
                            part_data1[curr_control] = 0;
                            start_new_state(checking_button);
                            claimed = true;
                        }
                }

                return claimed;
            }

            // Changes the current state to new_state and performs the (re-)initialisation necessary for new_state.
            // Note that certain states are instantaneous, in that their logic in this function will perform some
            // action and then finish with recursive call so we can start_new_state() on another state.
            void start_new_state(State new_state) {
                state = new_state;
                switch(state) {
                    case welcome:
                        tick = 50;
                        curr_control = 0;
                        set_text("...Starting...");
                        okay_button->enable(true); // XXX widget.hpp coding bug: enable(true) is actually disable(true).
                        okay_label->enable(true); 
                        previous_button->enable(true);
                        previous_label->enable(true);
                        finger_image->set_visible(false);
                        break;
                    case getting_button:
                        tick = 0;
                        finger_tick = 10;
                        okay_button->enable(true); 
                        okay_label->enable(true);
                        if(curr_control > 0) {
                            previous_button->enable(false);
                            previous_label->enable(false);
                        } else {
                            previous_button->enable(true);
                            previous_label->enable(true);
                        }
                        set_text("Please press [" + std::string(controls::control_names()[curr_control]) + "]");
                        break;
                    case checking_button:
                        finger_image->set_visible(false);
                        if(current_control_clashes()) {
                            start_new_state(already_used);
                        } else {
                            start_new_state(confirming_got_button);
                        }
                        break;
                    case already_used:
                        tick = 60;
                        set_text("You have already used that.");
                        break;
                    case confirming_got_button:
                        tick = 30;
                        set_text("Got action for [" + std::string(controls::control_names()[curr_control]) + "]");
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
                        set_text("All done! Press Okay to save.");
                        std::cerr << "Got (kind,id,polarity): ";
                        for(int c = 0; c < controls::NUM_CONTROLS; c++) {
                            std::cerr << "(" << part_kinds[c] << ", " << part_ids[c] << ", " << part_data0[c] << ", " << part_data1[c] << ") ";
                        }
                        std::cerr << std::endl;
                        break;
                }
            }


            // Compares the current (curr_control) joystick part signal with all the previous ones to see if it is the same as
            // any of them.  (So we can stop the player making CONTROL_LEFT and CONTROL_TONGUE the same button by accident.)
            bool current_control_clashes() {
                bool ret = false;
                for(int c = 0; c < curr_control; c++) {
                    ret = ret || clash(c, curr_control);
                }
                return ret;
            }


            // Compares the joystick part signals at positions control_one and control_two in the part_* arrays and
            // determines if they effectively the same.  Buttons clash if their ids are the same.  Axes clash if their ids are the
            // same and their low and high ranges cross.  Because this class only ever creates two ranges for an axis, [-large_mag,
            // -small_mag] and [small_mag, large_mag] it is enough just to test if the low values of the two axes are the same. Hats
            // clash if they have the same id and position (low) value. 
            bool clash(int control_one, int control_two) {
                return  (   part_kinds[control_one] == part_kinds[control_two] 
                        &&  part_ids[control_one] == part_ids[control_two]
                        &&  (   part_kinds[control_one] == joystick::BUTTON
                            ||  part_data0[control_one] == part_data0[control_two]
                            )
                        );
            }


            // Called every gui update cycle.  Implements our time-impervious ticks as described at their declaration.
            virtual void handle_process() override {
                gui::graphical_font_label::handle_process();
                if(tick > 0) {
                    tick--;
                    if(tick == 0) {
                        start_new_state(next_state(state));
                    }
                } else {
                    if(state == getting_button) {
                        finger_tick--;
                        if(finger_tick <= 0) {
                            finger_tick = 10;
                            finger_image->set_visible(!finger_image->visible());
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

    // Close the dialog
    void end_dialog(gui::dialog* d) {
        joystick::set_silent(false);
        d->close();
    }

    // 1) Actually use the mapping the user has just created for the in-game joystick, and
    // 2) store it in preferences::.
    void confirm_remapping(gui::dialog* d) {
        static_cast<JoystickMonitoringLabel*>(instruction_label.get())->effect_remap();
        end_dialog(d);
    }

    void do_draw_scene() {
        draw_scene(level::current(), last_draw_position());
    }

}

// Create and run the dialog
void show_joystick_configure_dialog()
{
	
	int button_width = 220;//232;
	int button_height = 45;//50;
    int label_height = 45;
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
    graphical_font_label* previous_label_raw = new graphical_font_label(_("Change previous button"), "door_label", 2);
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
                +   label_height            // Configuration
                +   padding                 //
                +   label_height            // Push UP now
                +   padding                 //
                +   button_height           // Redo Previous button
                +   padding                 //
                +   button_height           // Cancel   ||    Okay
                +   outer_padding           //
                ;

	dialog d((preferences::virtual_screen_width()/2 - window_w/2) & ~1, (preferences::virtual_screen_height()/2 - window_h/2) & ~1, window_w, window_h);
	d.set_padding(padding);
	d.set_background_frame("empty_window");
	d.set_upscale_frame(upscale_dialog_frame);
	d.set_draw_background_fn(do_draw_scene);

    // Create okay and cancel buttons
    button* okay_button_raw = new button(okay_label, boost::bind(confirm_remapping, &d), BUTTON_STYLE_NORMAL, button_resolution);
	widget_ptr okay_button(okay_button_raw); 
	okay_button->set_dim(button_width, button_height);
    okay_button->set_disabled_opacity(disabled_opacity);
    okay_label->set_disabled_opacity(disabled_opacity);
    
    button* cancel_button_raw = new button(cancel_label, boost::bind(end_dialog, &d), BUTTON_STYLE_NORMAL, button_resolution);
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
    JoystickMonitoringLabel* jml_raw = new JoystickMonitoringLabel("Awaiting your input.", "door_label", 2, 
            okay_button_raw, okay_label_raw, cancel_button_raw, finger_raw, previous_button_raw, previous_label_raw);
    instruction_label = widget_ptr(jml_raw);
    instruction_label->set_dim(2*button_width + padding, button_height);
	ASSERT_LOG(instruction_label != NULL, "Couldn't create instruction label widget.");

    // Link previous_button to instruction_label
    previous_button_raw->set_click_handler(boost::bind(back_one_control, jml_raw));

    // Place widgets in dialog    
    d.set_padding(padding);
    d.add_widget(title_label, outer_padding, outer_padding);
    d.add_widget(finger);
    d.add_widget(instruction_label, outer_padding + finger_raw->width() + padding, outer_padding + title_label->height() + padding);
    d.add_widget(previous_button, outer_padding, outer_padding + title_label->height() + padding + instruction_label->height() + padding);
    d.add_widget(cancel_button, dialog::MOVE_RIGHT);
    d.add_widget(okay_button);


    d.show_modal();

	return;
}
