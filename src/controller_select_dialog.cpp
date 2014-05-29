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


    // The Controller Select Dialog allows the player to select which
    // controller they want to use in-game from a drop-down list of attached
    // joysticks plus the keyboard. 
    // 
    // It also has a button taking the player to the relevant joystick or
    // keyboard configuration dialog.  
    //
    // If the user connects or disconnects a joystick during the lifetime of
    // this dialog, a second Controller Scan Dialog may pop up to inform the
    // user that the list of available devices has changed.

namespace {

    void show_controller_scan_dialog();

    // Sends us back to the previous screen.
    void end_dialog(gui::dialog* d) {
        d->close();
    }

    void do_draw_scene() {
        draw_scene(level::current(), last_draw_position());
    }

    // Shows the joystick configure dialog if we are using a joystick,
    // otherwise goes straight to the keyboard configure dialog.
    void show_configure_dialog() {
        // XXX #if TARGET_OS_IPHONE || TARGET_IPHONE_SIMULATOR then what do we do? Does the iPhone version use funny virtual keyboard?
        if(!preferences::use_joystick()) {
            show_controls_dialog();
        } else {
            show_joystick_configure_dialog();
        }
    }
    
    // Currently active Controller Scan Dialog, and its independent clock.
    gui::dialog* curr_controller_scan_dialog;
    int controller_scan_tick;

    // Human readable joystick names and their SDL instance ids.
    std::shared_ptr<std::vector<std::string>> joystick_names;
    std::shared_ptr<std::vector<SDL_JoystickID>> joystick_ids;



    // Hotplugging
    //
    // Hotplugging is a bit of a pain for the Controller Select Dialog because it
    // means the content of the drop down controller select widget may need to
    // change while the user is trying to use it.  We cope with this by listening
    // for joystick connect and disconnect events, and then whisking the user away
    // to the Controller Scan Dialog for a couple of seconds so we can reconstruct
    // the Controller Select Screen in peace.  
    //
    // We create a special gui::dialog subclass for Controller Select to do the
    // listening for connect and disconnect events.  This hotplug_listening_dialog
    // then simply shuts itself (the Controller Select Dialog) and shows the
    // Controller Scan dialog when one of those events arrives.  No actual joystick
    // changes are made yet.
    //
    // The Controller Scan Dialog informs the user that the joystick list is being
    // updated for 120 update cycles and then in turn closes itself and runs the
    // Controller Select Dialog all over again.  
    //
    // Throughout this whole sequence, the only time we ever (re-)construct the
    // dropdown list of controllers or the underlying device list in the joystick
    // module is when the show_controller_select_dialog() function is called to
    // construct and display the Controller Select dialog.

    class hotplug_listening_dialog : public gui::dialog {
        public:
            using gui::dialog::dialog;
        protected:
            bool handle_event(const SDL_Event& event, bool claimed) override {
                claimed |= dialog::handle_event(event, claimed);
                switch(event.type) {
                    case SDL_JOYDEVICEADDED:
                    case SDL_JOYDEVICEREMOVED:
                        claimed = true;
                        close();
                        show_controller_scan_dialog();
                        break;
                }
                return claimed;
            }
    };


    // Creates and displays the Controller Scan Dialog.  After this dialog has
    // displayed for 'display_ticks' ticks, it opens the Controller Select Dialog
    // for us again.
    void show_controller_scan_dialog() {

        using namespace gui;

        // Time to display for.
        const int display_ticks = 120;
        
        // Layout parameters
        int outer_padding = 20;
        int padding = 12;//16;
        gui::BUTTON_RESOLUTION button_resolution = gui::BUTTON_SIZE_DOUBLE_RESOLUTION;
        bool upscale_dialog_frame = true;
        
        widget_ptr message_label;

        message_label = widget_ptr(new graphical_font_label(_("Updating controller list..."), "door_label", 2));

        ASSERT_LOG(message_label != NULL, "Couldn't create select label widget.");

        // Dialog itself - width and height calculations illustrate layout.
        int window_w = outer_padding + message_label->width() + outer_padding;
        int window_h =    outer_padding 
                        + message_label->height()      // "Checking availability" 
                        + outer_padding;

        dialog d(   (preferences::virtual_screen_width()/2 - window_w/2) & ~1, 
                    (preferences::virtual_screen_height()/2 - window_h/2) & ~1,
                    window_w,
                    window_h
        );
        curr_controller_scan_dialog = &d;
        d.set_padding(padding);
        d.set_background_frame("empty_window");
        d.set_upscale_frame(upscale_dialog_frame);
        d.set_draw_background_fn(do_draw_scene);

        // Set up timing and timer function
        controller_scan_tick = display_ticks;
        d.set_process_hook([]() {
                controller_scan_tick--;
                if(controller_scan_tick <= 0) {
                    end_dialog(curr_controller_scan_dialog);
                    show_controller_select_dialog();
                }
            }
        );

        // Place widgets in dialog	
        d.set_padding(padding);
        d.add_widget(message_label, outer_padding, outer_padding);

        d.show_modal();

        return;
    }

}


void show_controller_select_dialog()
{
    // Layout parameters
	int outer_padding = 20;
    int label_height = 30;
    int dropdown_height = 25;
	int button_width = 440;//232;
	int button_height = 45;//50;
	int padding = 12;//16;
	gui::BUTTON_RESOLUTION button_resolution = gui::BUTTON_SIZE_DOUBLE_RESOLUTION;
	bool upscale_dialog_frame = true;
	
    // Flags to suppress buttons on certain platforms
	bool show_return = true;
	bool show_select = true;
	bool show_configure = true;
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
	
    // Labels for buttons etc
    widget_ptr return_label;
	widget_ptr configure_label;
	widget_ptr select_label;

    select_label = widget_ptr(new graphical_font_label(_("Select Input Device"), "door_label", 2));
    configure_label = widget_ptr(new graphical_font_label(_("Configure"), "door_label", 2));
    return_label = widget_ptr(new graphical_font_label(_("Return to Pause Menu"), "door_label", 2));

	ASSERT_LOG(return_label != NULL, "Couldn't create return label widget.");
	ASSERT_LOG(configure_label != NULL, "Couldn't create configure label widget.");
	ASSERT_LOG(select_label != NULL, "Couldn't create select label widget.");

    // Dialog itself - width and height calculations illustrate layout.
	int window_w = outer_padding + button_width + outer_padding;
    int window_h =    outer_padding 
                    + label_height      // Select your controller
                    + padding 
                    + dropdown_height   // Drop-down with controllers listed
                    + padding 
                    + button_height     // Configure
                    + padding 
                    + button_height     // Return to Pause screen
                    + outer_padding;

	hotplug_listening_dialog d( (preferences::virtual_screen_width()/2 - window_w/2) & ~1, 
                                (preferences::virtual_screen_height()/2 - window_h/2) & ~1, 
                                window_w, 
                                window_h
    );
	d.set_padding(padding);
	d.set_background_frame("empty_window");
	d.set_upscale_frame(upscale_dialog_frame);
	d.set_draw_background_fn(do_draw_scene);

    // Buttons
	widget_ptr configure_button(new button(configure_label, show_configure_dialog, BUTTON_STYLE_NORMAL, button_resolution));
	widget_ptr return_button(new button(return_label, boost::bind(end_dialog, &d), BUTTON_STYLE_NORMAL, button_resolution));
	configure_button->set_dim(button_width, button_height);
	return_button->set_dim(button_width, button_height);

    // Assemble the dropdown list of controllers.  We put Keyboard at the top of the list, and adjust other indices accordingly.
    joystick::synchronise_device_list();
    joystick_names = std::shared_ptr<std::vector<std::string>>(joystick::joystick_names());
    joystick_ids = std::shared_ptr<std::vector<SDL_JoystickID>>(joystick::joystick_ids());
    joystick_names->insert(joystick_names->begin(), "Keyboard");
    joystick_ids->insert(joystick_ids->begin(), -1);

    dropdown_widget* select_dropdown = new dropdown_widget(*joystick_names, button_width, 20);
    select_dropdown->set_zorder(9);

    // Find the current controller in the list and select it.  If joysticks are turned off, then the current controller is the keyboard.
    int current_device_index;
    if(!preferences::use_joystick()) {
        current_device_index = 0;
    } else {
        current_device_index = 0;
        for(int j = 0; j < joystick_ids->size(); j++) {
            if((*joystick_ids)[j] == joystick::current_device()) {
                current_device_index = j;
                break;
            }
        }
    }

    select_dropdown->set_selection(current_device_index); 
   
    // Set up the handler so that we change controllers when the list selection is changed.  If the user selects a joystick,
    // then preferences::use_joystick is turned on and we change the joystick device.  If the user selects the keyboard,
    // then preferences::use_joystick is turned off. 
    int ret; // Unused dummy
    select_dropdown->set_on_select_handler(
            [&ret] (int selection,const std::string& s) {
                if(selection == 0) {
                    preferences::set_use_joystick(false);
                    ret = -1;
                } else {
                    preferences::set_use_joystick(true);
                    joystick::change_device(selection - 1);
                    ret = selection - 1;
                }
            }
    );

    // Place widgets in dialog	
    d.set_padding(padding);
    if(show_select) {
        d.add_widget(select_label, outer_padding, outer_padding);
        d.add_widget(select_dropdown);
    }
    d.add_widget(configure_button);
    if(show_return) {
        d.add_widget(return_button);
    }

    d.show_modal();

	return;
}
