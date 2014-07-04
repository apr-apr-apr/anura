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

    //
    // Initialisation
    //

    // manager is a wrapper class that initialises and shuts down the joystick
    // module.  Retained as an interim measure.  Should subsume
    // joystick_manager.
    struct manager {
        // Initialise and run joystick module.
        manager();
        // Shut down joystick module.
        ~manager();
    };
    

    //
    // Input reading fuctions 
    //

    // All return true if the relevant button / stick is currently pressed in
    // the right direction, or false otherwise.  Silently return false if the
    // no joystick device is in use, or silent mode is on.  Buttons 0, 1 and 2
    // correspond to CONTROL_ATTACK, CONTROL_JUMP and CONTROL_TONGUE
    // respectively.  Out of range buttons return false always.
    bool up();
    bool down();
    bool left();
    bool right();
    bool button(int n);
    
    // When silent mode is on/true, up(), down(), left() right() and button()
    // always return false, regardless of whether the controls are actually
    // being pressed.
    void set_silent(bool on);

    int iphone_tilt();
    
    // Read input from hardware and update SDL data structures.
    void update();

    // Respond to SDL joystick attach and joystick remove events.
    bool pump_events(const SDL_Event&,bool);


    //
    // Preference setting functions
    //

    // Writes the current device selection to the preferences:: module.
    // Potentially sets use_joystick, chosen_joystick_guid and
    // chosen_joystick_name.
    void set_joystick_selection_preferences();
   
    // Writes the configuration of the current device to the preferences::
    // module.  Sets all of the inner joystick-configuration preferences.
    void set_joystick_configuration_preferences();


    //
    // For creating joystick select screens  
    //
    
    // Update device lists to reflect the controllers currently plugged into
    // the system.  Should be called promptly after update() (or an SDL event
    // queue poll) to ensure that SDL itself is in synch.  Returns true if
    // this function had to change the list, false otherwise.
    bool synchronise_device_list();
    
    // Device lists.  Elements at position k correspond to device k.
    // joystick_names() gives a human readable name, joystick_ids() gives an
    // SDL device instance id.  Use synchronise_device_list to ensure the lists
    // are up to date, or else ensure that pump_events captures *every*
    // relevant event from the moment manager() initialises.  Caller takes
    // ownership.
    std::shared_ptr<std::vector<std::string>> joystick_names();
    std::shared_ptr<std::vector<SDL_JoystickID>> joystick_ids();
    
    // Return the id of the controller currently being used in-game.
    // Return no_id if none exists.
    SDL_JoystickID current_device_id();
    const static SDL_JoystickID no_id = -1;

    // Change the controller used by the player in-game to the device at  
    // device_position.  Positions correspond to the device lists described
    // above.  Use change_device(no_device) to select no joystick.
    void change_device(int device_position);
    const static int no_device = -2;         // -2 to avoid conflating with no_id
   
    // Whether the current joystick was setup using default values (true) or
    // values supplied by the user or the preferences file (false).
    bool using_default_config();

    // Returns true iff the current joystick can use the configuration saved in
    // preferences::.
    bool can_use_preferences_config();

    // Causes the current joystick, if any, to use the configuration stored in
    // preferences::, if possible according to can_use_preferences_config().
    void use_preferences_config();

    // Causes the current joystick, if any, to use the default configuration.  The
    // default will depend on the particular device.
    void use_default_config();


    //
    // For creating joystick configure screens
    //

    // These functions support the interactive configuration screen.  The
    // configuration screen asks the player to press each button they want to
    // use for up, down, left, right, attack, jump etc.  
    //
    // These functions must be called in order described, otherwise they will
    // silently fail.
    //
    // Start by making sure that a valid joystick is in use (not just the
    // keyboard), then call start_configurer().  
    //
    // First you need to establish neutral ranges for each axis on the
    // controller.  If you are lucky neutral_zones_known() will be true, and
    // you can simply call default_neutral_zones().  Otherwise you will need to
    // establish them manually: Call clear_neutral_zones(), tell the player to
    // do nothing, then repeatedly call examine_neutral_zones_tick() over
    // several milliseconds.  After that, neutral_zones_dangerous() will tell
    // you if the neutral zones look right or not - maybe the player didn't
    // leave the sticks in a neutral position, or maybe one of them is really
    // noisy? You can safely clear() and examine() again if you want to.
    // Whatever method you use, once you have established the neutral ranges, a
    // dead zone will automatically be padded around each one. 
    //
    // Now you are ready to start assigning controller actions to game actions.
    // Iterate over {up, down, left, ... jump...}.  Ask the player to press the
    // button they want for the current control ("press up now"), and as part
    // of your engine update cycle call listen_for_signal() every tick.
    //
    // listen_for_signal() will return 'still_listening' until it gets a
    // meaningful button/axis/hat press (outside the dead zone).  Eventually
    // when the player does input something, listen_for_signal will return one
    // of three values:
    // - duplicate, meaning the player has tried to use the same button twice,
    //   in which case you should tell them so and listen() again,
    // - success_continue, meaning we got a good button/axis/hat press, and are
    //   ready to listen() for the next control
    // - success_finished, meaning we got a good button/axis/hat press, and
    //   have finished going through all the controls. 
    //
    // After listen() returns success_finished you can call apply_configuration()
    // to apply the configuration to the current controller.  
    //
    // You can go back to the previous control using retreat() at any time.
    //
    // Call stop_configurer() at any time to finish and release resources.
   

    // Initialise configurer.
    void start_configurer();

    // Neutral zone examiners:
    //
    // These functions try to work out the neutral zone for each axis on a
    // controller (the range of values the axis might return when it appears to
    // be sitting still in the neutral position).

    // Returns true if the neutral zones for the controller are known and can
    // be set by default_neutral_zones().  This places a lot of trust in the
    // accuracy of driver software, so interpret with caution.
    bool neutral_zones_known();

    // Clear neutral zones for axes on the controller.
    void clear_neutral_zones(); 

    // Successive calls to examine_neutral_zones_tick will establish upper and
    // lower bounds on where each axis sits when it is neutral.  
    // - clear_neutral_zones() must be called first.
    // - Successive calls must be spread over an appropriate time frame.
    // - Requires the user to leave the stick in a neutral state.
    void examine_neutral_zones_tick();

    // Returns true if the neutral range for any axis is too big (defined as
    // small_mag in size or larger).  
    bool neutral_zones_dangerous();

    // Initialises the neutral zones with 'best guesses' about what they are.
    // Suitable if you know you have prior knowledge of the controller (eg a
    // standard controller on a console).
    void default_neutral_zones();
  
    enum listen_result {
        still_listening,
        duplicate,
        success_keep_going,
        success_finished
    };

    // Tries to find a control signal, like the player pressing button 23, for
    // the current in-game control.  See documentation above for return value.
    // Needs to be called over successive update cycles.  Calls
    // joystick::update().
    listen_result listen_for_signal();

    // Goes back to the previous in-game control.  Returns false if you are
    // already at the first control, true otherwise.  After retreat()ing you
    // must listen() again to move forward and finish.
    bool retreat();
   
    // Applies the created configuration to the controller that is currently in use.
    // Causes a run time error if listen_for_signal() is not 'success_finished'.
    void apply_configuration();

    // stop_configurer() frees the underlying resources.  Can be called to 
    // destroy the configurer at any time.  Safe to call even if no
    // corresponding start_configurer() has happened yet.
    void stop_configurer();


    
    //
    // Encodings for joystick components / signals / mappings
    //

    // Component types on a joystick.
    enum part_kind {
        AXIS,
        BUTTON,
        HAT
    };

    // Return somewhat sensible default values for joystick configuration in
    // preferences::.  Refer to joystick.cpp for a detailed explanation of how
    // joystick configuration preferences are encoded.
    int default_kind(int curr_control);
    int default_id(int curr_control, int kind);
    int default_data0(int curr_control, int kind);
    int default_data1(int curr_control, int kind);

    // Ensure that joystick configuration preference encodings are in legal
    // ranges for their data type.  Valid range for data0 depends on the 
    // validated run-time kind for the joystick component in question.
    int validate_kind(int candidate_kind);
    int validate_id(int candidate_id);
    int validate_data0(int candidate_low, int validated_kind); 
    int validate_data1(int candidate_data1);

}

#endif
