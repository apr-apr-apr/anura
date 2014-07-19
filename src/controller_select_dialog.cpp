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
#include "checkbox.hpp"
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
        // XXX System specific code is probably required here
        // #if TARGET_OS_IPHONE || TARGET_IPHONE_SIMULATOR 
        if(joystick::current_device_id() == joystick::no_id) {
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

    // Currently active Controller Select Dialog, its checkboxes for default
    // and saved config, and its dropdown list of controllers
    gui::dialog* curr_controller_select_dialog;
    gui::dropdown_widget* controller_dropdown;
    gui::checkbox* default_config_check;
    gui::checkbox* saved_config_check;



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

    // Default config checkbox click.
void default_clicked() {
    joystick::use_default_config();
    saved_config_check->set_checked(false);
}

    // Saved config checkbox click.
void saved_clicked() {
    joystick::use_preferences_config();
    default_config_check->set_checked(false);
}

    // Sets up the joystick configuration checkboxes so that they are disabled and invisible when the keyboard is
    // selected.
void synchronise_checkboxes_keyboard() {
    default_config_check->enable(true);
    default_config_check->set_visible(false);
    saved_config_check->enable(true);
    saved_config_check->set_visible(false);
}

    // Sets up the joystick configuration checkboxes for when a joystick is selected.
    //
    // We only enable them both when the user can choose between the saved configuration and the default.  We hide the
    // 'saved' checkbox when it is not available.  The default is visible but disabled.
void synchronise_checkboxes_joystick() {
    if(joystick::can_use_preferences_config()) {
        default_config_check->enable(false);
        default_config_check->set_visible(true);
        saved_config_check->enable(false);
        saved_config_check->set_visible(true);
        bool default_on = joystick::using_default_config();
        default_config_check->set_checked(default_on);
        saved_config_check->set_checked(!default_on);
    } else {
        default_config_check->enable(true);
        default_config_check->set_visible(true);
        default_config_check->set_checked(true);
        saved_config_check->enable(true);
        saved_config_check->set_visible(false);
    }
}

    // Aligns the selected controller in the dropdown list with the one that is
    // actually in use now, and likewise selects and displays the relevant
    // configuration checkboxes for that controller.
void set_dropdown_and_checkboxes_from_curr_controller() {
    
    int current_device_dropdown_position;
    
    // Find the current controller in the drop down list and select it.  If
    // joysticks are turned off, then the current controller is the keyboard.
    if(joystick::current_device_id() == joystick::no_id) {
        current_device_dropdown_position = 0;
        synchronise_checkboxes_keyboard();
    } else {
        current_device_dropdown_position = 0;
        for(int j = 0; j < joystick_ids->size(); j++) {
            if((*joystick_ids)[j] == joystick::current_device_id()) {
                current_device_dropdown_position = j;
                synchronise_checkboxes_joystick();
                break; 
            }
        }
    }

    controller_dropdown->set_selection(current_device_dropdown_position); 
}

    // This is the dropdown list event handler for when the user selects a different controller.
    // If the user selects a joystick, then preferences::use_joystick is turned on and we change
    // the joystick device.  If the user selects the keyboard, then preferences::use_joystick is
    // turned off. 
    //
    // This function also synchronises the configuration checkboxes.
    //
    // Returns the position of the chosen joystick in the joystick module's device list, or
    // joystick::no_device if only the keyboard was chosen.
int set_controller_and_checkboxes_from_dropdown_event(int selection, const std::string& s) {
    int ret;
    
    if(selection == 0) {
        joystick::change_device(joystick::no_device);
        joystick::set_joystick_selection_preferences();
        ret = joystick::no_device;
        synchronise_checkboxes_keyboard();
    } else {
        joystick::change_device(selection - 1);
        joystick::set_joystick_selection_preferences();
        ret = selection - 1;
        synchronise_checkboxes_joystick();
    }
    
    return ret;
}

    //
    // Assemble and run the Controller Select dialog.
    //
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
	widget_ptr configure_button_label;
	widget_ptr select_label;
	widget_ptr configure_check_label;
    widget_ptr default_label;
    widget_ptr saved_label;

    select_label = widget_ptr(new graphical_font_label(_("Select Input Device"), "door_label", 2));
    configure_button_label = widget_ptr(new graphical_font_label(_("Configure"), "door_label", 2));
    return_label = widget_ptr(new graphical_font_label(_("Return to Pause Menu"), "door_label", 2));

    configure_check_label = widget_ptr(new graphical_font_label(_("Configuration: "), "door_label", 2)); 
    default_label = widget_ptr(new graphical_font_label(_("Default"), "door_label", 2)); 
    saved_label = widget_ptr(new graphical_font_label(_("Saved"), "door_label", 2));

	ASSERT_LOG(return_label != NULL, "Couldn't create return label widget.");
	ASSERT_LOG(configure_button_label != NULL, "Couldn't create configure label widget.");
	ASSERT_LOG(select_label != NULL, "Couldn't create select label widget.");
	ASSERT_LOG(configure_check_label != NULL, "Couldn't create configure check label widget.");
	ASSERT_LOG(default_label != NULL, "Couldn't create default label widget.");
	ASSERT_LOG(saved_label != NULL, "Couldn't create saved label widget.");

    // Dialog itself - width and height calculations illustrate layout.
	int window_w = outer_padding + button_width + outer_padding;
    int window_h =    outer_padding 
                    + label_height      // Select your controller
                    + padding 
                    + dropdown_height   // Drop-down with controllers listed
                    + padding 
                    + label_height     // Configuration: [ ] Default [ ] Saved
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
    curr_controller_select_dialog = &d;
	d.set_padding(padding);
	d.set_background_frame("empty_window");
	d.set_upscale_frame(upscale_dialog_frame);
	d.set_draw_background_fn(do_draw_scene);

    // Buttons
	widget_ptr configure_button(
        new button( configure_button_label, 
                    []() { show_configure_dialog(); set_dropdown_and_checkboxes_from_curr_controller(); }, 
                    BUTTON_STYLE_NORMAL, 
                    button_resolution)
    );
	widget_ptr return_button(new button(return_label, boost::bind(end_dialog, &d), BUTTON_STYLE_NORMAL, button_resolution));
	configure_button->set_dim(button_width, button_height);
	return_button->set_dim(button_width, button_height);

    // Check boxes
    default_config_check = new checkbox(default_label, false, boost::bind(&default_clicked));
    saved_config_check = new checkbox(saved_label, false, boost::bind(&saved_clicked));
    widget_ptr default_config(default_config_check);
    widget_ptr saved_config(saved_config_check);

    // Assemble the dropdown list of controllers.  
    //
    // The joystick module keeps an ordered list of devices, whose names and
    // ids are provided in corresponding order by joystick_names() and
    // joystick_ids().  We take those lists and stick "Keyboard" and
    // 'no_id' at the front.  When the user selects drop-down-list item K,
    // they get either the keyboard, or the joystick at position K-1 in the
    // joystick module's device list. 
    joystick::synchronise_device_list();
    joystick_names = std::shared_ptr<std::vector<std::string>>(joystick::joystick_names());
    joystick_ids = std::shared_ptr<std::vector<SDL_JoystickID>>(joystick::joystick_ids());
    joystick_names->insert(joystick_names->begin(), "Keyboard");
    joystick_ids->insert(joystick_ids->begin(), joystick::no_id);

    dropdown_widget* select_dropdown = new dropdown_widget(*joystick_names, button_width, 20);
    //XXX memory leak: need to dispose of this
    select_dropdown->set_zorder(9);
    controller_dropdown = select_dropdown;
    set_dropdown_and_checkboxes_from_curr_controller();

   
    select_dropdown->set_on_select_handler(set_controller_and_checkboxes_from_dropdown_event);

    // Place widgets in dialog	
    d.set_padding(padding);
    if(show_select) {
        d.add_widget(select_label, outer_padding, outer_padding);
        d.add_widget(select_dropdown);
        d.add_widget(configure_check_label, dialog::MOVE_RIGHT);
        d.add_widget(default_config, dialog::MOVE_RIGHT);
        d.add_widget(saved_config);
    }
    d.add_widget(configure_button, outer_padding, outer_padding + label_height + padding + dropdown_height + padding + label_height + padding);
    if(show_return) {
        d.add_widget(return_button);
    }

    d.show_modal();

	return;
}
