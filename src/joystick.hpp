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
#ifndef JOYSTICK_HPP_INCLUDED
#define JOYSTICK_HPP_INCLUDED

#include <vector>
#include "variant.hpp"
#include "variant_utils.hpp"

namespace joystick {

    // Wrapper class responsible for initialising and shutting down joystick
    // module.  Retained as interim measure.  Should subsume joystick_manager.
    struct manager {
        // Initialise and run joystick module.
        manager();
        // Shut down joystick module.
        ~manager();
    };
    
    // Input reading fuctions. All return true if the relevant button / stick
    // is currently pressed in the right direction, or false otherwise.
    // Silently return false if the joystick is off in preferences, a NULL
    // device is is use as the joystick or silent mode is on.  Buttons 0, 1 and
    // 2 correspond to CONTROL_ATTACK, CONTROL_JUMP and CONTROL_TONGUE
    // respectively.  Out of range buttons return false always.
    bool up();
    bool down();
    bool left();
    bool right();
    bool button(int n);
    
    int iphone_tilt();
    
    // Read input from hardware and update SDL data structures.
    void update();

    // Respond to SDL joystick attach and joystick remove events.
    bool pump_events(const SDL_Event&,bool);

    // Update device lists to reflect the controllers currently plugged into
    // the system.  Should be called promptly after update() (or an SDL event
    // queue poll) to ensure that SDL itself is in synch.  Returns true if
    // this function had to change the list, false otherwise.
    bool synchronise_device_list();
    
    // Return the id of the controller currently being used in-game.
    // Return no_device if none exists.
    SDL_JoystickID current_device();
    const static int no_device = -1;

    // Device lists.  Elements at index k correspond to device k.
    // joystick_names() gives a human readable name, joystick_ids() gives an
    // SDL device instance id.  Use synchronise_device_list to ensure the lists
    // are up to date, or else ensure that pump_events captures *every*
    // relevant event from the moment manager() initialises.  Caller takes
    // ownership.
    std::shared_ptr<std::vector<std::string>> joystick_names();
    std::shared_ptr<std::vector<SDL_JoystickID>> joystick_ids();
    
    // Change the controller used by the player in-game to the device at index
    // local_joystick_index. 
    void change_device(int local_joystick_index);
   
    // Change the button-to-in-game-control configuration of the controller
    // currently in use.
    void change_mapping(const int* part_kinds, const int* part_ids, const int* part_data0, const int* part_data1);
    
    // Component types on a joystick.
    enum part_kind {
        AXIS,
        BUTTON,
        HAT
    };

    // Our default joystick axis range values:  We consider an axis to be active on
    // the negative side when it is in the range [-large_mag, -small_mag], inactive
    // in the dead zone when it is in the range (-small_mag, small_mag) and active
    // on the positive side in the range [small_mag, large_mag].
    extern const int small_mag;
    extern const int large_mag;

    // Return somewhat sensible default values for joystick configuration in
    // preferences::.  Refer to joystick.cpp for a detailed explanation of how
    // joystick configuration preferences are encoded.
    int default_kind(int curr_control);
    int default_id(int curr_control, int kind);
    int default_low(int curr_control, int kind);
    int default_high(int curr_control, int kind);

    // Ensure that joystick configuration preference encodings are in legal
    // ranges for their data type.  Valid range for data0 depends on the 
    // validated run-time kind for the joystick component in question.
    int validate_kind(int candidate_kind);
    int validate_id(int candidate_id);
    int validate_data0(int candidate_low, int validated_kind); 
    int validate_data1(int candidate_data1);

    // When silent mode is on/true, up(), down(), left() right() and button()
    // always return false, regardless of whether the controls are actually
    // being pressed.
    void set_silent(bool on);

}

#endif
