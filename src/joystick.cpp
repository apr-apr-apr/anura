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
#include "graphics.hpp"

#include <iostream>
#include <map>
#include <memory>
#include <vector>

#include "asserts.hpp"
#include "filesystem.hpp"
#include "foreach.hpp"
#include "haptic.hpp"
#include "joystick.hpp"
#include "preferences.hpp"
#include "controls.hpp"

#if defined(TARGET_BLACKBERRY)
#include <bps/accelerometer.h>
#include <bps/sensor.h>
#include <bps/bps.h>
#endif

#include "asserts.hpp"

namespace haptic {
namespace {
std::map<int,std::shared_ptr<SDL_Haptic>> haptic_devices;
typedef std::map<SDL_Haptic*,std::map<std::string,int>> haptic_effect_table;
haptic_effect_table& get_effects() {
	static haptic_effect_table res;
	return res;
}
}
}

namespace joystick {

    // Useful values for defining axis ranges, neutral points and dead zones.
    //
    // SDL axes can occupy positions in the range [-32768, 32767].  Orientation,
    // neutral zones and dead zones are not necessarily known.
    //
    // When we need a default neutral zone, we work with a single point, either
    // zero, -32768 or 32767.
    //
    // When we need to work with a dead zone, we default to 4096 either side of
    // the neutral zone (inclusive). 

    namespace axval {
        const int lowest = -32768;  
        const int low_sentinel = lowest - 1;
        const int zero = 0;
        const int dead_pad = 4096;
        const int dead_pad_ex = dead_pad + 1; // dead_pad for an exclusive range
        const int highest = 32767;
        const int high_sentinel = highest + 1;
    }

    class sdl_controller;
        class sdl_joy_controller;
        class sdl_game_controller;

    class controller_signal;
        class union_signal;
        class real_controller_signal;
            class axis_signal;
            class button_signal;
            class hat_signal;

    class player_controller;

    class joystick_manager;


    class sdl_controller {
        // sdl_controller lets you read input and characteristics from a
        // connected game controller or joystick.  A wrapper class for
        // SDL_Joystick and SDL_GameController to unify these two closely
        // related elements... but only the bits we need.  Refer to SDL
        // documentation for basic concepts.

        public:
            // Start using the device at position 'device_position' in SDL's
            // list of currently connected joysticks.  Returns NULL if the
            // opening fails.
            static sdl_controller* open(int device_position);
            
            // The read_ functions return values in the same ranges as their
            // SDL equivalents.
            virtual Sint16 read_axis(int axis_id) = 0;
            virtual Uint8 read_button(int button_id) = 0;
            virtual Uint8 read_hat(int hat_id) = 0;

            // The num_functions return the number of relevant part in SDL's
            // model of the controller.
            virtual int num_axes() = 0;
            virtual int num_buttons() = 0;
            virtual int num_hats() = 0;
           
            // Get SDL device attributes.  get_id() returns this controller's
            // SDL *instance* ID, which is different to the device position 
            // parameter in open().  
            virtual SDL_JoystickID get_id() = 0; 
            virtual std::string get_guid() = 0;
            const int guid_buffer_size = 65;
            virtual std::string get_name() = 0;
            virtual bool is_open_and_attached() = 0;

            // These functions are our subjective hints about the best default
            // way to set the controller up.
            virtual bool prefer_axial_setup() = 0;
            virtual bool prefer_hatty_setup() = 0;
            virtual bool prefer_sdl_gc_setup() = 0;

            // Returns true if we know the ideal neutral positions for all axes.
            virtual bool know_neutral_points() = 0;

            virtual ~sdl_controller() {
            }

    };


    class sdl_joy_controller : public sdl_controller {
        // sdl_joy_controller is the SDL_Joystick version of sdl_controller.
        
        public:
            sdl_joy_controller(SDL_Joystick* joystickIN) {
                joystick = joystickIN;
            }

            Sint16 read_axis(int axis_id) override {
                return SDL_JoystickGetAxis(joystick, axis_id);
            }
            
            Uint8 read_button(int button_id) override {
                return SDL_JoystickGetButton(joystick, button_id);
            }
            
            Uint8 read_hat(int hat_id) override {
                return SDL_JoystickGetHat(joystick, hat_id);
            }

            int num_axes() override {
                return SDL_JoystickNumAxes(joystick);
            }

            int num_buttons() override {
                return SDL_JoystickNumButtons(joystick);
            }

            int num_hats() override {
                return SDL_JoystickNumHats(joystick);
            }
            
            SDL_JoystickID get_id() override {
                return SDL_JoystickInstanceID(joystick);
            }
            
            std::string get_guid() {
                char guid_buffer[guid_buffer_size];
                SDL_JoystickGUID guid = SDL_JoystickGetGUID(joystick);
                SDL_JoystickGetGUIDString(guid, guid_buffer, guid_buffer_size);
                return std::string(guid_buffer);
            }
            
            std::string get_name() {
                return std::string(SDL_JoystickName(joystick));
            }

            bool is_open_and_attached() override {
                return SDL_JoystickGetAttached(joystick);
            }
           
            // These are subjective hints about default setups.  Remember that SDL can call a d-pad anything from 4 buttons to a
            // 9-way hat to two axes.  We just check what SDL thinks it has so later we can guess a vaguely sensible default
            // setup.  We only prefer the gc (game controller style) setup if we are using an SDL_GameController, so that's
            // always false in this class.
            bool prefer_axial_setup() override {
                return SDL_JoystickNumAxes(joystick) >= 2;
            }
            bool prefer_hatty_setup() override {
                return SDL_JoystickNumHats(joystick) >= 1;
            }
            bool prefer_sdl_gc_setup() override {
                return false;
            }

            // As a general rule, if we know enough about a controller to know the neutral points of its axes, we will probably
            // set it up as an SDL_GameController, rather than trying to recognise it here.  However, if we know that a
            // particular platform has a highly consistent set of drivers, ie a console, we might return true here for the
            // entire platform. 
            bool know_neutral_points() override {
                return false;
            }
            
            ~sdl_joy_controller() {
                SDL_JoystickClose(joystick);
            }
        
        private:
            SDL_Joystick* joystick;

    };


    class sdl_game_controller : public sdl_controller {
        // The SDL_GameController version of sdl_controller.  SDL_GameController
        // itself sits on top of SDL_Joystick, so many of the underlying
        // functions here are still SDL_Joystick calls.

        public:
            sdl_game_controller(SDL_GameController* game_controllerIN, SDL_Joystick* joystickIN) {
                game_controller = game_controllerIN;
                joystick = joystickIN;
            }

            Sint16 read_axis(int axis_id) override {
                return SDL_GameControllerGetAxis(game_controller, (SDL_GameControllerAxis) axis_id);
            }
            
            Uint8 read_button(int button_id) override {
                return SDL_GameControllerGetButton(game_controller, (SDL_GameControllerButton) button_id);
            }

            int num_axes() override {
                return SDL_CONTROLLER_AXIS_MAX;
            }

            int num_buttons() override {
                return SDL_CONTROLLER_BUTTON_MAX;
            }

            // SDL_GameController has no hats, so this function should never be
            // called. If it is, it always returns SDL_HAT_CENTERED. 
            Uint8 read_hat(int hat_id) override {
                return SDL_HAT_CENTERED;
            }

            int num_hats() override {
                return 0;
            }

            SDL_JoystickID get_id() override {
                return SDL_JoystickInstanceID(joystick);
            }

            std::string get_guid() {
                char guid_buffer[guid_buffer_size];
                SDL_JoystickGUID guid = SDL_JoystickGetGUID(joystick);
                SDL_JoystickGetGUIDString(guid, guid_buffer, guid_buffer_size);
                return std::string(guid_buffer);
            }

            std::string get_name() {
                return std::string(SDL_GameControllerName(game_controller));
            }
            
            bool is_open_and_attached() override {
                return SDL_GameControllerGetAttached(game_controller);
            }

            // SDL_GameController models our device as a virtual XBox360 pad.
            // Flagging that with prefer_sdl_gc_setup means the default config
            // function can assign controls to buttons very sensibly.
            bool prefer_axial_setup() override {
                return false;
            }
            bool prefer_hatty_setup() override {
                return false;
            }
            bool prefer_sdl_gc_setup() override {
                return true;
            }

            // At the time of coding, the design SDL_GameController was still
            // being adjusted.  However, we do know that a *properly* defined
            // SDL_GameController uses zero as the (ideal) netural point for all
            // axes.
            bool know_neutral_points() override {
                return true;
            }

            ~sdl_game_controller() {
                SDL_GameControllerClose(game_controller);
            }

        private:
            SDL_GameController* game_controller;
            SDL_Joystick* joystick;
    };
    
    // open(device_position) opens the device at position 'device_position' in
    // the SDL joystick device list.  If that device can function as an
    // SDL_GameController that version is used, otherwise the SDL_Joystick
    // version is used.  Will return NULL if the open()ing fails. 
    sdl_controller* sdl_controller::open(int device_position) {
        if(SDL_IsGameController(device_position)) {
            SDL_GameController* gc = SDL_GameControllerOpen(device_position);
            if(gc == NULL) {
                std::cerr << "Warning: Failed to open SDL_GameController at device position " << device_position << "." << std::endl;
                return NULL;
            } else {
                SDL_Joystick* jc = SDL_GameControllerGetJoystick(gc);
                if(jc == NULL) {
                    std::cerr << "Warning: Failed to open SDL_GameController underlying joystick at device at position " << device_position << "." << std::endl;
                    return NULL;
                } else {
                    std::cerr << "INFO: Opened device at position " << device_position << " as sdl_game_controller." << std::endl;
                    return new sdl_game_controller(gc, jc);
                }
            }
        } else {
            SDL_Joystick* jc = SDL_JoystickOpen(device_position);
            if(jc == NULL) {
                std::cerr << "Warning: Failed to open SDL_Joystick device at position " << device_position << "." << std::endl;
                return NULL;
            } else {
                std::cerr << "INFO: Opened device at position " << device_position << " as sdl_joy_controller." << std::endl;
                return new sdl_joy_controller(jc);
            }
        }
    }

    
    class controller_signal : public std::enable_shared_from_this<controller_signal> {
        // We use a map from joystick/gamepad actions to in-game control
        // actions so that players can customise their controls.
        //
        // A controller_signal identifies a physical action, such as Button 3
        // being pressed on pad 24, for example, and can determine if that
        // action is happening (firing) now.  
        
        public:

            // is_firing() checks whether SDL thinks the player is currently
            // pressing this control the way we are looking for.
            virtual bool is_firing() = 0;
            
            // cs.realise() returns cs if cs is a real_controller_signal,
            // otherwise it returns real_controller_signal that is a good
            // subjective approximation.  (This is a bit of an ugly hack for
            // when we want to remove the complexity of layered
            // controller_signals.)  Caller shares ownership.
            virtual std::shared_ptr<real_controller_signal> realise() = 0;
           
            virtual ~controller_signal() {}
    };


    class union_signal : public controller_signal {
        // union_signal is used when we want two or more physical actions (like
        // pushing up on the left stick and pushing up on the right stick) to
        // map be read as a single signal (just up).  You can use unions of
        // unions.

        protected:
            std::shared_ptr<controller_signal> primary;
            std::shared_ptr<controller_signal> secondary;

        public:
            static std::shared_ptr<union_signal> make(std::shared_ptr<controller_signal> primary_in, std::shared_ptr<controller_signal> secondary_in) {
                union_signal* ret = new union_signal();
                ret->primary = primary_in;
                ret->secondary = secondary_in;
                return std::shared_ptr<union_signal>(ret);
            }

            bool is_firing() override {
                return primary->is_firing() || secondary->is_firing();
            }

            // What if we want to turn this into just one
            // real_controller_signal?  This union will form a tree whose
            // leaves are all real_controller_signals, so we just pick the left
            // most leaf.
            virtual std::shared_ptr<real_controller_signal> realise() override {
                return primary->realise();
            }
    };


    class real_controller_signal : public controller_signal {
        // A real_controller_signal identifies a single button being pressed (or a
        // single axis being moved one way etc) on a particular joystick/gamepad.
        // Button 3 being pressed on pad 24, for example.  
        //
        // real_controller_signal has 3 exhaustive and exclusive subclasses,
        // axis_signal, button_signal and hat_signal whose types are manually
        // tracked as part_kinds AXIS, BUTTON, and HAT.
        //
        // real_controller_signals can be encoded and stored with the
        // preferences:: module. 
        
        protected:
            // The SDL component id of this component on the controller.
            int id;
            
            // The controller we're looking on. 
            std::shared_ptr<sdl_controller> device;
            
            // Human readable names for axis/button/hat
            static const char* kind_names[];
        
        public:
            // Returns the part_kind, ie axis/button/hat.
            virtual part_kind get_kind() = 0;

            // Returns the SDL component id for the axis/button/hat that we are
            // interested in.
            virtual int get_id() {
                return id;
            }

            // Return data0 and data1 used to save this controller_signal in
            // preferences.
            virtual int get_data0() {
               return 0;
            }

            virtual int get_data1() {
               return 0;
            } 

            // Human readable kind name. 
            const std::string get_kind_name() {
                return kind_names[get_kind()];
            }

            // Get the sdl_controller that we are interested in.  Caller shares
            // ownership.
            std::shared_ptr<sdl_controller> get_device() {
                return device;
            }

            // realise() doesn't need to do anything to turn this object into a
            // real_controller_signal, we can just return it directly. 
            virtual std::shared_ptr<real_controller_signal> realise() {
                return std::static_pointer_cast<real_controller_signal>(shared_from_this());
                // That's "return this;" with shared_ptrs in C++11.
            }
            
            // Creates a controller signal using the given joystick 'device',
            // representing the button or axis identified by 'part_id'
            // being pressed (in the case of a button or hat) or being
            // positioned between low and high inclusive (in the case of an
            // axis).  Whether the component is an axis/button/hat is denoted
            // by 'part_type'.  The caller takes ownership. May return NULL
            // if input is out of range etc.
            static std::shared_ptr<controller_signal> make(
                    std::shared_ptr<sdl_controller> device, part_kind part_type, int part_id, int low, int high);

            virtual ~real_controller_signal() {}
    };

    const char* real_controller_signal::kind_names[] = { "axis", "button", "hat" };

    
    
    class axis_signal : public real_controller_signal {

        // axis_signal models a joy axis being pressed in one particular
        // direction.  For example, a left-right axis being pushed left.
        //
        // 'axis' is the SDL id for the axis on the hardware device.  Our idea of
        // the axis is digital, but SDL represents all axes as analogue.  So we
        // consider the player to be pressing the axis the way we want if the
        // axis's value is in the inclusive range [low, high].
        //
        // SDL and its underlying drivers do not necessarily account for axis
        // orientation, ideal neutral points or dead zones.  You can often work
        // around these issues simply by choosing 'low' and 'high' based on
        // direct input from the user.

        private:
            int low;  
            int high;

        public:
            axis_signal(std::shared_ptr<sdl_controller> device_in, int id_in, int low_in, int high_in) { 
                
                device = device_in;  
                
                id = id_in; 
                low = low_in;
                high = high_in;
            }

            part_kind get_kind() override {
                return AXIS;
            }

            bool is_firing() override {
                Sint16 axisPos = device->read_axis(id);
                return (low <= axisPos) && (axisPos <= high);
            }

            int get_low() {
                return low;
            }

            int get_high() {
                return high;
            }

            virtual int get_data0() override {
                return get_low();
            }

            virtual int get_data1() override {
                return get_high();
            }
    };


    class button_signal : public real_controller_signal {
        // button_signal models a controller button being pressed.  
        
        public:
            button_signal(std::shared_ptr<sdl_controller> device_in, int id_in) {
                device = device_in;
                id = id_in;
            }
            
            part_kind get_kind() override {
                return BUTTON;
            }

            bool is_firing() override {
                return device->read_button(id);
            }

    };


    class hat_signal : public real_controller_signal {
        // hat_signal models a controller hat being pressed.  
        //
        // What SDL and the driver call a hat is often a d-pad.  Players and
        // application programmers usually think of a d-pad as a pair of
        // digital axes.  On a d-pad 'up-left' happens by pressing up on the
        // vertical axis and left on the horizontal axis together.  But SDL's
        // hat interface detects 'up-left' as an atomic position that is
        // mutually exclusive with all other positions on the hat, including
        // 'up' and 'left'.  
        //
        // What this means is that when hat_signal is trying to work out if the
        // hat is 'left' in the Anura sense, it has to ask SDL if the hat is
        // 'left', or 'up left' or 'down left'.
        // 
        // We express this in general terms by looking round the d-pad
        // clockwise and calling the position we want 'middle', the one in
        // front of it 'front' and the one behind it 'back'.  In the case of
        // 'left', front=left-up, middle=left, back=left-down. 

        private:
            Uint8 front;    
            Uint8 middle;  
            Uint8 back;   

        public:
            hat_signal(std::shared_ptr<sdl_controller> device_in, int id_in, int middle_in) { 
                
                device = device_in;  
                
                id = id_in;

                if(middle_in == SDL_HAT_CENTERED) {
                    std::cerr << "Warning: centred hat is not a valid signal" << std::endl;
                    middle_in = SDL_HAT_LEFT;
                }

                middle = middle_in;
                front = middle_to_front(middle_in);
                back = middle_to_back(middle_in);
            }

            part_kind get_kind() override {
                return HAT;
            }

            bool is_firing() override {
                Uint8 hat_pos = device->read_hat(id);
                return hat_pos == front || hat_pos == middle || hat_pos == back;
            }

            int getMiddle() {
                return middle;
            }

            virtual int get_data0() override {
                return getMiddle();
            }

            // Given the Anura direction we are looking for (eg 'up') returns the
            // direction in front of that, clockwise, that we should also 
            // check on the hat (eg 'right-up').
            static Uint8 middle_to_front(int middle) {
                switch(middle) {
                    case SDL_HAT_RIGHT:
                        return SDL_HAT_RIGHTDOWN;
                    case SDL_HAT_RIGHTDOWN:
                        return SDL_HAT_DOWN;
                    case SDL_HAT_DOWN:
                        return SDL_HAT_LEFTDOWN;
                    case SDL_HAT_LEFTDOWN:
                        return SDL_HAT_LEFT;
                    case SDL_HAT_LEFT:
                        return SDL_HAT_LEFTUP;
                    case SDL_HAT_LEFTUP:
                        return SDL_HAT_UP;
                    case SDL_HAT_UP:
                        return SDL_HAT_RIGHTUP;
                    case SDL_HAT_RIGHTUP:
                        return SDL_HAT_RIGHT;
                } 
                ASSERT_FATAL("Hat middle out of range.");
            }

            // Given the Anura direction we are looking for (eg 'up') returns the
            // direction behind that, clockwise, that we should also 
            // check on the hat (eg 'left-up').
            static Uint8 middle_to_back(int middle) {
                switch(middle) {
                    case SDL_HAT_RIGHT:
                        return SDL_HAT_RIGHTUP;
                    case SDL_HAT_RIGHTUP:
                        return SDL_HAT_UP;
                    case SDL_HAT_UP:
                        return SDL_HAT_LEFTUP;
                    case SDL_HAT_LEFTUP:
                        return SDL_HAT_LEFT;
                    case SDL_HAT_LEFT:
                        return SDL_HAT_LEFTDOWN;
                    case SDL_HAT_LEFTDOWN:
                        return SDL_HAT_DOWN;
                    case SDL_HAT_DOWN:
                        return SDL_HAT_RIGHTDOWN;
                    case SDL_HAT_RIGHTDOWN:
                        return SDL_HAT_RIGHT;
                } 
                ASSERT_FATAL("Hat middle out of range.");
            }
    };



    std::shared_ptr<controller_signal> real_controller_signal::make(
        std::shared_ptr<sdl_controller> device, part_kind kind, int part_id, int data0, int data1) {

        switch(kind) {
            case AXIS:
                return std::make_shared<axis_signal>(device, part_id, data0, data1);
            case BUTTON:
                return std::make_shared<button_signal>(device, part_id);
            case HAT:
                return std::make_shared<hat_signal>(device, part_id, data0);
            default:
                std::cerr << "Warning: part_kind " << kind << " is out of range." << std::endl;
                return NULL;
        }

    }
   

    // How Joystick Settings are saved in Preferences
    //
    // The preferences:: module allows us to read and write settings from the
    // user's preferences.cfg file.
    // 
    // This joystick module set()s and get()s the following settings:
    //
    // bool use_joystick
    //      Indicates whether we are using the joystick to control gameplay.
    //      The joystick module will still load and try to find joysticks
    //      regardless of this setting.  The keyboard will still act as the
    //      always-on base controller regardless.  Other joystick preferences
    //      will still be read / created and saved regardless of this setting.
    //
    // string chosen_joystick_guid
    //      An SDL GUID identifying the model of controller last chosen by the
    //      user.  An empty string where not available.
    //
    // string chosen_joystick_name
    //      (Supposedly) human-readable name of the joystick last chosen by the
    //      user.  May be empty or a dummy value.  Minimal effect on
    //      functionality. 
    //
    // The rest of the settings relate to custom configuration of the joystick.
    // At this stage of development we only allow one configuration to be
    // saved.
    //
    // string configured_joystick_guid
    //      An SDL GUID identifying the model of controller whose custom
    //      mapping we have saved.  An empty string where not available.
    //      NOTE: Does NOT indicate which joystick was last used. 
    //
    // string configured_joystick_name
    //      (Supposedly) human-readable name of the joystick these settings
    //      apply to.  May be empty or a dummy value.  Minimal effect on
    //      functionality. 
    //
    // int joystick_up_part_kind    joystick_down_part_kind     ...     
    //     joystick_up_part_id      joystick_down_id            ...     
    //     joystick_up_part_data0   joystick_down_data0         ...     
    //     joystick_up_part_data1   joystick_down_data1         ...     
    //
    //     For each of the Anura in-game controls, up, down, left, right,
    //     attack, jump and tongue, these settings state which part of the
    //     controller (used in what way) corresponds to that control.  The
    //     general form is joystick_[ANURA_CONTROL]_part_[ASPECT].
    //
    //     The kinds are the part_kinds {AXIS, BUTTON, HAT} cast as ints.
    //
    //     The ids are SDL joystick part IDs in the range 0 to 255.
    //     
    //     data0 and data1 are ints - assumed to be within 32 bit int range -
    //     that depend on the part.
    //     
    //          For AXIS data0 is 'low' and data1 is 'high'.
    //     
    //          For BUTTON data0 and data1 are meaningless.
    //     
    //          For HAT data0 is the hat position value (see SDL documentation)
    //          and data1 is meaningless.
    //
    // The preferences:: module will always try to load all of these settings,
    // calling this joystick:: module to (partially) validate the retrieved
    // values or provide defaults where none exist.
    //
    // With the exception of use_joystick, all other settings are only ever
    // written back into the preferences:: module when the user selects or
    // configures their own joystick in the options menus.
    //
    // Note 1: There is a UNION controller_signal, but to keep things simple it
    // cannot be encoded in the preferences file.  The motivation for having
    // the union_signal is to allow Anura to second guess what the user wants
    // to press - letting them use several axes for up and down for example.
    // But if we are storing custom preferences, we know exactly which
    // axis/button/hat the user wants to use, so there is no pressing need to
    // second guess.  
    

    class player_controller {
        // player_controller handles the situation of player one (the player on
        // the local system) using a controller that has various sticks/buttons
        // that are mapped to Anura's in-game controls.   

        private:
            // The SDL device the player is using.
            std::shared_ptr<sdl_controller> device;

            // Our map from Anura controls (controls::CONTROL_JUMP) to device input (button 21 on joystick 3).
            // When in use this vector should always have controls::NUM_CONTROLS elements.
            std::vector<std::shared_ptr<controller_signal>> signal_map;

            // Whether this controller's configuration, as defined by signal_map, was created by default here (true)
            // or created externally (ie from preferences or a config screen) (false).
            bool default_config;

        public:
            player_controller() {
                device = NULL;
                signal_map.clear();
                default_config = true;
            }

            // get_device() shares ownership of the returned device.
            std::shared_ptr<sdl_controller> get_device() {
                return device;
            }

            bool is_default_config() {
                return default_config;
            }

            // Overwrites signal_map with controller_signals created from the user
            // preferences file.  If no sdl_controller is in use, no map is
            // created.
            void configure_from_preferences() {
                if(device == NULL) {
                    return;
                }
                std::cerr << "INFO: Using configuration loaded from preferences::." << std::endl;
                signal_map.clear();
                for(int c = 0; c < controls::NUM_CONTROLS; c++) {
                    signal_map.push_back(
                        real_controller_signal::make(device, (part_kind) preferences::joy_part_kind(c), 
                            preferences::joy_part_id(c), preferences::joy_part_data0(c), preferences::joy_part_data1(c))
                    ); 
                }
                default_config = false;
            }

            // Updates the preferences::joystick_something variables indicate
            // which joystick the user has selected.  Doesn't change any saved
            // configuration.
            void set_preferences_from_chosen_device() {
                if(device == NULL) {
                    preferences::set_use_joystick(false);
                } else {
                    preferences::set_use_joystick(true);
                    preferences::set_chosen_joystick_guid(device->get_guid());
                    preferences::set_chosen_joystick_name(device->get_name());
                }
            }
            
            // Updates the preferences::joystick_something variables relating
            // to joystick configuration to reflect the way the joystick is
            // currently set up.  If no sdl_controller is in use, no update
            // takes place.
            void set_preferences_from_configuration() {
                if(device == NULL) {
                    return;
                }
                preferences::set_configured_joystick_guid(device->get_guid());
                preferences::set_configured_joystick_name(device->get_name());
                for(int c = 0; c < controls::NUM_CONTROLS; c++) {
                    preferences::set_joy_part_kind(c, signal_map[c]->realise()->get_kind());
                    preferences::set_joy_part_id(c, signal_map[c]->realise()->get_id());
                    preferences::set_joy_part_data0(c, signal_map[c]->realise()->get_data0());
                    preferences::set_joy_part_data1(c, signal_map[c]->realise()->get_data1());
                }
            }

            // change_device(new_device) resets this player_controller to start
            // using the new_device.  It removes any references to the old
            // controller device we were using, including clearing out the
            // controller_signals map.  A new map is created: if the new device
            // matches the joystick settings in preferences::, then those
            // settings will be used.  Otherwise a default configure_blind()
            // mapping is used.
            void change_device(std::shared_ptr<sdl_controller> new_device) {
                device = new_device;
                if(device != NULL) {
                    std::cerr << "INFO: Now using controller " << device->get_name() << " [" << device->get_guid() << "]." << std::endl;
                    ///RRR preferences::set_use_joystick(true);
                    if(device->get_guid() == preferences::configured_joystick_guid()) {
                        configure_from_preferences();
                    } else {
                        configure_blind();
                    }
                } else {
                    std::cerr << "INFO: Now using NO controller." << std::endl;
                    ///RRR preferences::set_use_joystick(false);
                }
            }

            // change_mapping() creates a new mapping for the current
            // controller and discards the old mapping.  It does *not* alter
            // the joystick settings in preferences::.  If no device is
            // currently in use this function has no effect.  
            void change_mapping(const int* part_kinds, const int* part_ids, const int* part_data0, const int* part_data1) {
                if(device == NULL) {
                    return;
                }
                std::cerr << "INFO: Using altered controller configuration." << std::endl;
                default_config = false;
                signal_map.clear();
                for(int c = 0; c < controls::NUM_CONTROLS; c++) {
                    signal_map.push_back(
                            real_controller_signal::make(device, (part_kind) part_kinds[c], part_ids[c], part_data0[c], part_data1[c])
                    );
                }
            }

            // Creates a default mapping for the current device.
            //
            // If no device is in use, this function will do nothing.
            // 
            // The various device->prefer_() functions are used to guess what an approriate setup might be.
            void configure_blind() {
                using namespace axval;

                if(device == NULL) {
                    return;
                }
              
                default_config = true;

                signal_map.clear();
                 
                // Case one:  we are using a virtual XBox360 controller that SDL_GameController has set up.
                // We can be pretty confident here about the layout of the actual device.
                if(device->prefer_sdl_gc_setup()) {
                    std::cerr << "INFO: Using default configuration style sdl_gc_setup." << std::endl;
                    signal_map.push_back(
                            union_signal::make(
                                real_controller_signal::make(device, BUTTON, SDL_CONTROLLER_BUTTON_DPAD_UP, 0, 0),
                                union_signal::make(
                                    real_controller_signal::make(device, AXIS, SDL_CONTROLLER_AXIS_LEFTY, lowest, -dead_pad_ex),
                                    real_controller_signal::make(device, AXIS, SDL_CONTROLLER_AXIS_RIGHTY, lowest, -dead_pad_ex)
                                )
                            )
                    ); 
                    signal_map.push_back(
                            union_signal::make(
                                real_controller_signal::make(device, BUTTON, SDL_CONTROLLER_BUTTON_DPAD_DOWN, 0, 0),
                                union_signal::make(
                                    real_controller_signal::make(device, AXIS, SDL_CONTROLLER_AXIS_LEFTY, dead_pad_ex, highest),
                                    real_controller_signal::make(device, AXIS, SDL_CONTROLLER_AXIS_RIGHTY, dead_pad_ex, highest)
                                )
                            )
                    ); 
                    signal_map.push_back(
                            union_signal::make(
                                real_controller_signal::make(device, BUTTON, SDL_CONTROLLER_BUTTON_DPAD_LEFT, 0, 0),
                                union_signal::make(
                                    real_controller_signal::make(device, AXIS, SDL_CONTROLLER_AXIS_LEFTX, lowest, -dead_pad_ex),
                                    real_controller_signal::make(device, AXIS, SDL_CONTROLLER_AXIS_RIGHTX, lowest, -dead_pad_ex)
                                )
                            )
                    ); 
                    signal_map.push_back(
                            union_signal::make(
                                real_controller_signal::make(device, BUTTON, SDL_CONTROLLER_BUTTON_DPAD_RIGHT, 0, 0),
                                union_signal::make(
                                    real_controller_signal::make(device, AXIS, SDL_CONTROLLER_AXIS_LEFTX, dead_pad_ex, highest),
                                    real_controller_signal::make(device, AXIS, SDL_CONTROLLER_AXIS_RIGHTX, dead_pad_ex, highest)
                                )
                            )
                    );
                    signal_map.push_back(real_controller_signal::make(device, BUTTON, SDL_CONTROLLER_BUTTON_A, 0, 0)); 
                    signal_map.push_back(real_controller_signal::make(device, BUTTON, SDL_CONTROLLER_BUTTON_B, 0, 0)); 
                    signal_map.push_back(real_controller_signal::make(device, BUTTON, SDL_CONTROLLER_BUTTON_Y, 0, 0)); 

                // Case two:  we've got something with a hat, which is probably a d-pad.  
                } else if(device->prefer_hatty_setup()) {
                    std::cerr << "INFO: Using default configuration style hatty_setup." << std::endl;
                    signal_map.push_back(real_controller_signal::make(device, HAT, 0, SDL_HAT_UP, 0)); 
                    signal_map.push_back(real_controller_signal::make(device, HAT, 0, SDL_HAT_DOWN, 0)); 
                    signal_map.push_back(real_controller_signal::make(device, HAT, 0, SDL_HAT_LEFT, 0));
                    signal_map.push_back(real_controller_signal::make(device, HAT, 0, SDL_HAT_RIGHT, 0));
                    signal_map.push_back(real_controller_signal::make(device, BUTTON, 0, 0, 0)); 
                    signal_map.push_back(real_controller_signal::make(device, BUTTON, 1, 0, 0)); 
                    signal_map.push_back(real_controller_signal::make(device, BUTTON, 2, 0, 0)); 

                // Case three: there's no hat, so the main stick or d-pad is probably on the first two axes.    
                } else { // device->prefer_axial_setup() or nothing
                    std::cerr << "INFO: Using default configuration style axial_setup." << std::endl;
                    signal_map.push_back(real_controller_signal::make(device, AXIS, 1, lowest, -dead_pad_ex));
                    signal_map.push_back(real_controller_signal::make(device, AXIS, 1, dead_pad_ex, highest)); 
                    signal_map.push_back(real_controller_signal::make(device, AXIS, 0, lowest, -dead_pad_ex));
                    signal_map.push_back(real_controller_signal::make(device, AXIS, 0, dead_pad_ex, highest));
                    signal_map.push_back(real_controller_signal::make(device, BUTTON, 0, 0, 0)); 
                    signal_map.push_back(real_controller_signal::make(device, BUTTON, 1, 0, 0)); 
                    signal_map.push_back(real_controller_signal::make(device, BUTTON, 2, 0, 0)); 
                }
            }

            // Input reading fuctions.
            //
            // All return true if the relevant button / stick is currently pressed in the right direction or false otherwise.
            // Silently return false if no meaningful map exists.
            // Return false always if preferences::use_joystick is off.

            bool up() {
                if(!device) {
                    return false;
                }
                return signal_map[controls::CONTROL_UP]->is_firing();
            }

            bool down() {
                if(!device) {
                    return false;
                }
                return signal_map[controls::CONTROL_DOWN]->is_firing();
            }

            bool left() {
                if(!device) {
                    return false;
                }
                return signal_map[controls::CONTROL_LEFT]->is_firing();
            }

            bool right() {
                if(!device) {
                    return false;
                }
                return signal_map[controls::CONTROL_RIGHT]->is_firing();
            }

            // Buttons 0, 1 and 2 correspond to CONTROL_ATTACK, CONTROL_JUMP and CONTROL_TONGUE respectively.
            bool button(int n) {
                if(!device) {
                    return false;
                }
                switch(n) {
                    case 0:
                        return signal_map[controls::CONTROL_ATTACK]->is_firing();
                    case 1:
                        return signal_map[controls::CONTROL_JUMP]->is_firing();
                    case 2:
                        return signal_map[controls::CONTROL_TONGUE]->is_firing();
                    default:
                        return false;
                }

                return false;
            }

    };


    class interactive_controller_configurer {
        private:
            std::shared_ptr<sdl_controller> device;

            int curr_control;
                        // See joystick.cpp's documentation for how preferences are stored. 
            int part_kinds[controls::NUM_CONTROLS];
            int part_ids[controls::NUM_CONTROLS];
            int part_data0[controls::NUM_CONTROLS];
            int part_data1[controls::NUM_CONTROLS]; 
      
            std::vector<int> neutral_min;
            std::vector<int> neutral_max;
            
            // inside(a, b, c) returns true if a <= b <= c and false otherwise.
            bool inside(int a, int b, int c) {
                if(b > c || b < a) {
                    return false;
                }
                return true;
            }

            // Compares the joystick part signals at positions control_one and control_two in the part_* arrays and
            // determines if they effectively the same.  Buttons clash if their ids are the same.   Hats clash if they
            // have the same id and position (low) value. Axes clash if their ids are the same and their low and high
            // ranges cross. (We check the low and high points of each range fall outside the other range.) 
            bool clash(int control_one, int control_two) {
                return  (   part_kinds[control_one] == part_kinds[control_two] 
                        &&  part_ids[control_one] == part_ids[control_two]
                        &&  (   part_kinds[control_one] == joystick::BUTTON
                            ||  (   part_kinds[control_one] == joystick::HAT
                                &&  part_data0[control_one] == part_data0[control_two]
                                )
                            ||  (   part_kinds[control_one] == joystick::AXIS
                                &&  (   inside(part_data0[control_one], part_data0[control_two], part_data1[control_one]) 
                                    ||  inside(part_data0[control_one], part_data1[control_two], part_data1[control_one])
                                    ||  inside(part_data0[control_two], part_data0[control_one], part_data1[control_two])
                                    ||  inside(part_data0[control_two], part_data1[control_one], part_data1[control_two])
                                    )
                                )
                            )
                        );
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

            listen_result check_signal(bool got_signal) {
                if(!got_signal) {
                    return still_listening;
                }
                if(current_control_clashes()) {
                    return duplicate;
                }
                curr_control++;
                if(curr_control >= controls::NUM_CONTROLS) {
                    curr_control--;
                    return success_finished;
                } else {
                    return success_keep_going;
                }
            }

        public:

            interactive_controller_configurer(std::shared_ptr<sdl_controller> i_device) {
                device = i_device;
                if(device == NULL) {
                    ASSERT_FATAL("Cannot interactively configure NULL device.");
                }
                curr_control = 0;
                neutral_max.resize(device->num_axes(), 0);
                neutral_min.resize(device->num_axes(), 0);
            }

            // Default neutral zone ranges.
            void default_neutral_zones() {
                for(int idx = 0; idx < device->num_axes(); idx++) {
                    neutral_min[idx] = 0;
                    neutral_max[idx] = 0;
                }
            }

            // Initialises neutral zone ranges.
            void clear_neutral_zones() {
                for(int idx = 0; idx < device->num_axes(); idx++) {
                    neutral_min[idx] = axval::high_sentinel;
                    neutral_max[idx] = axval::low_sentinel;
                }
            }

            // Scans all axes, assuming they are sitting in a neutral position,
            // and updates the neutral ranges.  Call successively to account
            // for noisy axes.
            void neutral_zones_tick() {
                for(int idx = 0; idx < device->num_axes(); idx++) {
                    int curr_val = device->read_axis(idx);
                    if(curr_val < neutral_min[idx]) {
                        neutral_min[idx] = curr_val;
                    }
                    if(curr_val > neutral_max[idx]) {
                        neutral_max[idx] = curr_val;
                    }
                }
            }

            // We declare the axis to be abnormally noisy if in the (supposed)
            // neutral position it varies by dead_pad or more.
            // This might mean the player has bumped an axis while we were
            // scanning it, so the result is dangerous.
            bool neutral_zones_dangerous() {
                print_neutral_zones();
                bool abnormal = false;
                for(int idx = 0; idx < device->num_axes(); idx++) {
                    abnormal = abnormal || (neutral_max[idx] - neutral_min[idx] >= axval::dead_pad);
                }
                return abnormal;
            }

            void print_neutral_zones() {
                using namespace axval;
                std::cerr << "===Axis Neutral Zones===" << std::endl;
                for(int idx = 0; idx < device->num_axes(); idx++) {
                    std::cerr   << "axis " << idx << ": [" << neutral_min[idx] << ", " << neutral_max[idx] 
                                << "] for dead zone of [" << neutral_min[idx] - dead_pad << ", " 
                                << neutral_max[idx] + dead_pad << "]." << std::endl;
                }
                std::cerr << "Dead zones are defined by padding neutral zones by " << dead_pad << " either side." << std::endl;
                std::cerr << "Edit preferences.cfg directly to set completely arbitrary active input ranges." << std::endl;
                std::cerr << "------------------------" << std::endl;
            }


            listen_result listen_for_signal() {
                using namespace axval;

                update();
                
                // See if there is anything interesting on the axes.
                for(int idx = 0; idx < device->num_axes(); idx++) {
                    int high_active_start = neutral_max[idx] + dead_pad_ex;
                    int low_active_start = neutral_min[idx] - dead_pad_ex;
                    int val = device->read_axis(idx);
                    if(val <= low_active_start) {
                        part_kinds[curr_control] = joystick::AXIS;
                        part_ids[curr_control] = idx;
                        part_data0[curr_control] = lowest;
                        part_data1[curr_control] = low_active_start;
                        return check_signal(true);
                    } else if(val >= high_active_start) {
                        part_kinds[curr_control] = joystick::AXIS;
                        part_ids[curr_control] = idx;
                        part_data0[curr_control] = high_active_start;
                        part_data1[curr_control] = highest;
                        return check_signal(true);
                    } 
                }

                // The buttons
                for(int idx = 0; idx < device->num_buttons(); idx++) {
                    if(device->read_button(idx)) {
                        part_kinds[curr_control] = joystick::BUTTON;
                        part_ids[curr_control] = idx;
                        part_data0[curr_control] = 0;
                        part_data1[curr_control] = 0;
                        return check_signal(true);
                    }
                }

                // The hats
                for(int idx = 0; idx < device->num_hats(); idx++) {
                    int val = device->read_hat(idx);
                    if(val != SDL_HAT_CENTERED) {
                        part_kinds[curr_control] = joystick::HAT;
                        part_ids[curr_control] = idx;
                        part_data0[curr_control] = val;
                        part_data1[curr_control] = 0;
                        return check_signal(true);
                    }
                }

                return check_signal(false);
            }


            bool retreat() {
                curr_control--;
                if(curr_control < 0) {
                    curr_control++;
                    return false;
                }
                return true;
            }

            int* get_part_kinds() {
                return part_kinds;
            }

            int* get_part_ids() {
                return part_ids;
            }

            int* get_part_data0() {
                return part_data0;
            }

            int* get_part_data1() {
                return part_data1;
            }
    };


    class joystick_manager {

        public:
            // joystick_names() returns a human-readable list of names for the joysticks currently connected through SDL.  
            //
            // In practice the names may not be readable - "???" is given when no name could be found.  The caller takes
            // ownership of the list.
            std::shared_ptr<std::vector<std::string>> joystick_names() {
                std::shared_ptr<std::vector<std::string>> ret = std::make_shared<std::vector<std::string>>();
                for(auto stick : joysticks) {
                    std::string curr_name = stick->get_name();
                    if(curr_name.length() > 0) {
                        ret->push_back(curr_name);
                    } else {
                        ret->push_back("???");
                    }
                }
                return ret;
            }

            // joystick_ids() returns the *instance* ids associated with the controllers currently connected through SDL.
            // 
            // Joystick positions in joystick_ids() and joystick_names() match up.  The caller takes ownership of the list.
            std::shared_ptr<std::vector<SDL_JoystickID>> joystick_ids() {
                std::shared_ptr<std::vector<SDL_JoystickID>> ret = std::make_shared<std::vector<SDL_JoystickID>>();
                for(auto stick : joysticks) {
                    ret->push_back(stick->get_id());
                }
                return ret;
            }

            // change_device(device_position) gets the local_player_controller to use the joystick designated by
            // 'device_position'.  Any device it was previously using will be unceremoniously discarded.
            //
            // Alternatively, calling change_device(no_device) will cause the local_player_controller to use no
            // joystick.
            // 
            // 'device_position' refers to the postion of the chosen controller in 'joysticks', joystick_ids() and
            // joystick_names().  
            void change_device(int device_position) {
                if(device_position == no_device) {
                    local_player_controller->change_device(NULL);
                } else {
                    ASSERT_INDEX_INTO_VECTOR(device_position, joysticks);
                    local_player_controller->change_device(joysticks[device_position]);
                }
            }

            // device_id() returns the *instance* id for the current controller.
            // If there is no current controller, joystick::no_id;
            SDL_JoystickID device_id() {
                std::shared_ptr<sdl_controller> device = local_player_controller->get_device();
                if(device) {
                    return device->get_id();
                } else {
                    return no_id;
                }
            }

            
            void initial_setup();
            
            bool pump_events(const SDL_Event& ev, bool claimed);
          
            // synch_devices() makes sure that our current list of available
            // (connected and open) joystick devices is up-to date. 
            // It returns true if it adds or removes any joysticks to
            // the list, or false otherwise.
            //
            // An SDL joystick update should be called very shortly before
            // calling this function. (Polling the SDL event queue will do
            // this, but it may be advisable to call SDL_JoystickUpdate()
            // immediately prior.)
            //
            // This is a fix-it function because it's not currently realistic
            // for joystick_manager to rely on receiving every SDL joystick
            // attach and remove event.
            
            bool synch_devices() {

                bool ret = false;

                // Check that each joystick we have opened is still actually
                // connected.  If not, see if we have to clear it from the
                // player_controller, and chuck it.

                auto iter = joysticks.begin();
                while(iter != joysticks.end()) {
                    if(!(*iter)->is_open_and_attached()) {
                        if(local_player_controller->get_device() == (*iter)) {
                            local_player_controller->change_device(NULL);
                        }
                        iter = joysticks.erase(iter);
                        ret = true;
                    } else {
                        iter++;
                    }
                }

                // All right.  This is a bit embarrassing.  SDL really wants us to
                // track every joystick and attach and remove event, but that
                // doesn't gel nicely with Anura at the moment.  We can potentially
                // lose those events, so we need to have a synchronisation check
                // before we tell the user which joysticks are available etc.  
                //
                // To complicate matters, the SDL2 api is still evolving and is
                // not very well documented.  There doesn't appear to be any
                // way you can directly ask SDL "is device 14 open".  
                //
                // We can work around this in an ungainly manner by opening
                // device 14 (again), getting its instance id and then checking
                // if that instance id is used by any other stick we've got
                // open.  SDL does support multiple open()s and close()s of the
                // same device, so it appears to work, but oh for an
                // SDL_JoystickIsOpen(int device_id) function.
                
                // We go through each device in SDL's physical device list, see
                // if it's open, and if not open it and add it to our list of
                // open controllers.
                int num_sticks = SDL_NumJoysticks();
                for(int j = 0; j < num_sticks; j++) {
                    std::shared_ptr<sdl_controller> candidate(sdl_controller::open(j));
                    if(!candidate) {
                        ASSERT_FATAL("SDL Joystick refuses to open detected stick.");
                    }
                    SDL_JoystickID candidate_id = candidate->get_id();
                    bool found = false;
                    for(std::shared_ptr<sdl_controller> con : joysticks) {
                        if(con->get_id() == candidate_id) {
                            found = true;
                            break;
                        }
                    }
                    if(!found) {
                        joysticks.push_back(std::shared_ptr<sdl_controller>(candidate));
                        ret = true;
                    }
                }

                return ret;
            }

            ~joystick_manager() {
                local_player_controller = NULL; // Should delete underlying player_controller
                joysticks.clear(); // Should SDL_ThingyClose() underlying joysticks
            }

        private:
            std::vector<std::shared_ptr<sdl_controller>> joysticks;
            std::shared_ptr<player_controller> local_player_controller;
    };

    // pump_events(event, already_claimed) checks the given SDL event to see if it
    // is relevant to the joysticks we are managing.  
    //
    // If claimed is already true, no check is made and we return true.  Otherwise,
    // if the event is relevant (a joystick add or remove event), the event is
    // processed and claimed by returning true.  If checking shows the event is not
    // relevant, the event is left unclaimed by returning false.
    //
    // If a joystick has been added, our joystick_manager will open it and add it
    // to the list of available devices.
    //
    // If a joystick has been removed, our joystick_manager will erase it
    // (triggering an SDL_Close()).  If the device the player is currently using is
    // removed, the player_controller for that device will be destroyed too. 
    bool joystick_manager::pump_events(const SDL_Event& ev, bool claimed) {
        if(claimed) {
            return claimed;
        }
       
        // SDL generates attach and remove events for GameControllers too, but they
        // are duplicate events on top of the joystick events SDL will generate for
        // the underlying joysticks anyway.  So in the absence of any reason to
        // treat GameControllers differently, we just ignore those superfluous
        // events.

        switch(ev.type) {
            case SDL_JOYDEVICEADDED: {
                int joy_index = ev.jdevice.which;
                sdl_controller* new_controller = sdl_controller::open(joy_index);
                if(!new_controller) {
                    std::cerr << "Warning: Tried to open new joy/game controller at device INDEX " << joy_index << " ... but SDL wouldn't!" << std::endl;
                } else {
                    std::cerr << "INFO: Added new controller from SDL device INDEX " << joy_index << "." << std::endl;
                    joysticks.push_back(std::shared_ptr<sdl_controller>(new_controller));
                }
                return true;
            }

            case SDL_JOYDEVICEREMOVED: {
                // What a nuisance.  One of the controllers has been ripped out. Now we need to 1)
                // rid it from the player_controller if the player was using it and 2) rid it from
                // the joystick list.
                SDL_JoystickID joy_id = ev.jdevice.which;
                
                auto iter = joysticks.begin();
                bool did_find = false;
                bool was_in_use = false;
                while(iter != joysticks.end()) {
                    if((*iter)->get_id() == joy_id) {
                        std::shared_ptr<sdl_controller> device_in_use_now = local_player_controller->get_device();
                        if(device_in_use_now != NULL && device_in_use_now->get_id() == joy_id) {
                            local_player_controller->change_device(NULL);
                            was_in_use = true;
                        }
                        iter = joysticks.erase(iter);
                        did_find = true;
                        break;
                    } else {
                        iter++;
                    }
                }
                if(!did_find) {
                    std::cerr << "Warning: Tried to remove controller identified as SDL instance ID " << joy_id << ", but SDL wouldn't!" << std::endl;
                } else {
                    std::cerr << "INFO: Removed joy/game controller identified as ID " << joy_id << (was_in_use ? ", which was in use." : ", (not in use).") << std::endl;
                }
                return true;
            }
        }
        return false;
    }


    namespace {
        // Note: These shared pointers will control objects that have custom
        // destructors containingSDL_ThingyClose() calls.  Being in file/namespace
        // scope, they will also live till the end of the program.  So we need to
        // be sure to clear() these collections in order to invoke the underlying
        // destructors before we try calling SDL_Quit or anything else that will
        // invalidate calls to SDL_ThingyClose().

        joystick_manager* local_manager = NULL;
        player_controller* local_joystick = NULL;
        interactive_controller_configurer* local_configurer = NULL;
        bool silent = false;
    }


    // Sets up signal_map from the user preferences that have been read
    // in from the user configuration file. We rely on the preferences reading
    // process already having created sensible defaults to cover preferences
    // that were missing in the file.
    void joystick_manager::initial_setup() {

        if(SDL_InitSubSystem(SDL_INIT_JOYSTICK) != 0) {
            std::cerr << "ERROR: Unable to initialise joystick subsystem" << std::endl;
            // We can pretty much abandon hope of using a joystick now.  Just
            // create a non-functional player_controller and leave.
            local_player_controller = std::make_shared<player_controller>();
            local_joystick = local_player_controller.get();
            local_configurer = NULL;
            return;
        }

        if(SDL_InitSubSystem(SDL_INIT_GAMECONTROLLER) != 0) {
            std::cerr << "ERROR: Unable to initialise game controller subsystem" << std::endl;
        } else {

            // In the future Anura should probably load a central
            // SDL_GameController database, but for now we just rely on SDL's
            // inbuilt db (from whenever SDL was compiled, maybe ages ago) and load
            // the user's own SDL_GameController configuration database, if it
            // exists
            // XXX temporary code so we can use older SDL with Mac OS
#if !TARGET_OS_MAC
            std::string user_sdl_gamecontroller_db = std::string(preferences::user_data_path()) + "/sdl_gamecontrollerdb.txt";
            if(sys::file_exists(user_sdl_gamecontroller_db)) {
                int db_result = SDL_GameControllerAddMappingsFromFile(user_sdl_gamecontroller_db.c_str());
                if(db_result == -1) {
                    std::cerr << "Warning: SDL not happy with " << user_sdl_gamecontroller_db << ".  Persisting." << std::endl;
                } else {
                    std::cerr << "SDL found " << db_result << " interesting game controller descriptions in [" << user_sdl_gamecontroller_db << "]." << std::endl;
                }
            } else {
                std::cerr << "There is no user game controller database called [" << user_sdl_gamecontroller_db << "]." << std::endl;
            }
#endif
        }

        if(SDL_InitSubSystem(SDL_INIT_HAPTIC) != 0) {
            std::cerr << "ERROR: Unable to initialise haptic subsystem" << std::endl;
        }
        
        // Now we're going to open every joystick we can grab (except on Android
        // where we stick to just the first - is that the system stick?)
#if defined(__ANDROID__)
        int n = 0; {
#else
        for(int n = 0; n != SDL_NumJoysticks(); ++n) {
#endif
            std::shared_ptr<sdl_controller> j = std::shared_ptr<sdl_controller>(sdl_controller::open(n));
            if(j) {
                joysticks.push_back(j);
            } else {
                std::cerr << "Warning: could not open SDL_Joystick at position " << n << " in device list." << std::endl;
            }

            SDL_Haptic *haptic = SDL_HapticOpen(n);
            if(haptic) {
                // Note: The SDL_HapticClose() in our custom destructor must be called before we shut SDL down.
                // haptic::haptic_devices may survive until the end of the program, though, so we need to explicitly
                // clear it out before shutting SDL down.
                haptic::haptic_devices[n] = std::shared_ptr<SDL_Haptic>(haptic, [](SDL_Haptic* h){SDL_HapticClose(h);});
                if(SDL_HapticRumbleInit(haptic) != 0) {
                    std::cerr << "Failed to initialise a simple rumble effect" << std::endl;
                    haptic::haptic_devices.erase(n);
                }
                // buzz the device when we start.
                if(SDL_HapticRumblePlay(haptic, 0.5, 1000) != 0) {
                    std::cerr << "Failed to play a simple rumble effect" << std::endl;
                    haptic::haptic_devices.erase(n);
                }
            }
        }

        std::cerr << "INFO: Initialized " << joysticks.size() << " joysticks" << std::endl;
        std::cerr << "INFO: Initialized " << haptic::haptic_devices.size() << " haptic devices" << std::endl;

        // Make the player_controller that links our hardware controls to in-game controls 
        local_player_controller = std::make_shared<player_controller>();

        // If joysticks are on in preferences::, then choose one to use.
        if(preferences::use_joystick()) {
            // See if the joystick saved in preferences is connected now.  If not, we'll settle for the
            // first available stick, if there is one.  If there are no sticks, we'll turn joysticks
            // off in preferences::.  Will probably want to decouple preferences use_joystick() by
            // using device != NULL or device_id != no_id in future.
            std::shared_ptr<sdl_controller> chosen_stick = NULL;
            if(preferences::chosen_joystick_guid().length() > 0) { // empty string indicates that we have no particular saved preferences
                for(auto curr_stick : joysticks) {
                    if(curr_stick->get_guid() == preferences::chosen_joystick_guid()) {
                        chosen_stick = curr_stick;
                        break;
                    }
                }
            }

            if(chosen_stick != NULL) {
                local_player_controller->change_device(chosen_stick);
            } else if(joysticks.size() > 0) {
                local_player_controller->change_device(joysticks[0]);
            } else {
                //preferences::set_use_joystick(false); // XXX just do nothing here
            }
        }

        // Hacky way to link in to the singular interface.
        local_joystick = local_player_controller.get();
        local_configurer = NULL;
    }


 



    // validate_something() functions for ensuring that preferences:: values are
    // in-range.
    //
    // validate_something(x, context) will return x if it is valid given 'context',
    // otherwise it will return an arbitrary valid value.

    int validate_kind(int candidate_kind) {
        switch(candidate_kind) {
            case AXIS:
            case BUTTON:
            case HAT:
                return candidate_kind;
            default:
                return BUTTON;           
        }
    }

    int validate_id(int id) {
        if(id < 0 || id > 255) {
            return 0;
        }
        return id;
    }

    int validate_data0(int data0, int kind) {
        switch(kind) {
            case AXIS:
                return data0;
            case BUTTON:
                return data0;
            case HAT:
                switch(data0) {             
                    case SDL_HAT_UP:
                    case SDL_HAT_DOWN:
                    case SDL_HAT_LEFT:
                    case SDL_HAT_RIGHT:
                    case SDL_HAT_LEFTUP:
                    case SDL_HAT_LEFTDOWN:
                    case SDL_HAT_RIGHTUP:
                    case SDL_HAT_RIGHTDOWN:
                        return data0;
                    default:
                        return SDL_HAT_RIGHT;
                }
            default:
                ASSERT_FATAL("kind out of range when validating low");
                break;
        }
    }

    int validate_data1(int data1) {
        return data1;
    }


    // default_something() functions return default values for *preferences* only.
    // They are here to fill in incomplete preferences - usually the result of
    // someone manually editing the preferences and cutting bits out.
    //
    // default_ASPECT(control, context) returns the default ASPECT of 'control'
    // given 'context'.  
    //
    // There is no guarantee that the defaults will be sensible
    // or interact sensibly with other pre-existing settings.
    //
    // Will fail if 'control' is out of range.

    int default_kind(int curr_control) {
        using namespace controls;
        switch(curr_control) {
            case CONTROL_UP:
            case CONTROL_DOWN:
            case CONTROL_LEFT:
            case CONTROL_RIGHT:
                return AXIS;
            case CONTROL_ATTACK:
            case CONTROL_JUMP:
            case CONTROL_TONGUE:
                return BUTTON;
            default:
                ASSERT_FATAL("curr_control out of range.");        
        }
    }
            
    int default_id(int curr_control, int kind) {
        using namespace controls;
        switch(kind) {
            case AXIS:
                switch(curr_control) {
                    case CONTROL_UP:
                    case CONTROL_DOWN:
                        return 1;
                    case CONTROL_LEFT:
                    case CONTROL_RIGHT:
                        return 0;
                    case CONTROL_ATTACK:
                        return 2;
                    case CONTROL_JUMP:
                        return 3;
                    case CONTROL_TONGUE:
                        return 4;
                    default:
                        ASSERT_FATAL("curr_control out of range.");
                }
                break;
            case BUTTON:
                switch(curr_control) {
                    case CONTROL_UP:
                        return 3;
                    case CONTROL_DOWN:
                        return 4;
                    case CONTROL_LEFT:
                        return 5;
                    case CONTROL_RIGHT:
                        return 6;
                    case CONTROL_ATTACK:
                        return 0;
                    case CONTROL_JUMP:
                        return 1;
                    case CONTROL_TONGUE:
                        return 2;
                    default:
                        ASSERT_FATAL("curr_control out of range.");
                }
                break;
            case HAT:
                switch(curr_control) {
                    case CONTROL_UP:
                    case CONTROL_DOWN:
                    case CONTROL_LEFT:
                    case CONTROL_RIGHT:
                        return 0;
                    case CONTROL_ATTACK:
                    case CONTROL_JUMP:
                    case CONTROL_TONGUE:
                        return 1;
                    default:
                        ASSERT_FATAL("curr_control out of range.");
                }
                break;
            default:
                ASSERT_FATAL("kind out of range");
        }
    }

    int default_data0(int curr_control, int kind) {
        using namespace controls;
        using namespace axval;
        switch(kind) {
            case AXIS:
                switch(curr_control) {
                    case CONTROL_UP:
                        return lowest;
                    case CONTROL_DOWN:
                        return dead_pad_ex;
                    case CONTROL_LEFT:
                        return lowest;
                    case CONTROL_RIGHT:
                        return dead_pad_ex;
                    case CONTROL_ATTACK:
                        return dead_pad_ex;
                    case CONTROL_JUMP:
                        return dead_pad_ex;
                    case CONTROL_TONGUE:
                        return dead_pad_ex;
                    default:
                        ASSERT_FATAL("curr_control out of range.");
                }
                break;
            case BUTTON:
                return 0;
                break;
            case HAT:
                switch(curr_control) {
                    case CONTROL_UP:
                        return SDL_HAT_UP;
                    case CONTROL_DOWN:
                        return SDL_HAT_DOWN;
                    case CONTROL_LEFT:
                        return SDL_HAT_LEFT;
                    case CONTROL_RIGHT:
                        return SDL_HAT_RIGHT;
                    case CONTROL_ATTACK:
                        return SDL_HAT_DOWN;
                    case CONTROL_JUMP:
                        return SDL_HAT_RIGHT;
                    case CONTROL_TONGUE:
                        return SDL_HAT_UP;
                    default:
                        ASSERT_FATAL("curr_control out of range.");
                }
                break;
            default:
                ASSERT_FATAL("kind out of range");
        }
    }

    int default_data1(int curr_control, int kind) {
        using namespace controls;
        using namespace axval;
        switch(kind) {
            case AXIS:
                switch(curr_control) {
                    case CONTROL_UP:
                        return -dead_pad_ex;
                    case CONTROL_DOWN:
                        return highest;
                    case CONTROL_LEFT:
                        return -dead_pad_ex;
                    case CONTROL_RIGHT:
                        return highest;
                    case CONTROL_ATTACK:
                        return highest;
                    case CONTROL_JUMP:
                        return highest;
                    case CONTROL_TONGUE:
                        return highest;
                    default:
                        ASSERT_FATAL("curr_control out of range.");
                }
                break;
            case BUTTON:
            case HAT:
                return 0;
                break;
            default:
                ASSERT_FATAL("kind out of range");
        }
    }


    // 
    // Singluar Interface
    //
    // Even though joystick_manager could be easily adapted to support multiple
    // players, Anura doesn't presently support more than one local player.
    // Other modules are in the habit of using joystick:: functions to access
    // the singular joystick.  This singular interface is maintained here,
    // passing on all the relevant calls to a joystick_manager.
    //
    // The only additional functionality the singular interface provides is
    // silent mode. 
    //

    // When silence is on, all the singlular device input functions will always
    // return false.  Direct input reading calls through player_controller and
    // joystick_manager are unaffected, as is direct collection of SDL input
    // events.
    void set_silent(bool new_val) {
        silent = new_val;
    }

    //
    // Singluar to joystick_manager calls.  See header and joystick_manager documentation.
    //

    bool synchronise_device_list() {
        return local_manager->synch_devices();
    }

    std::shared_ptr<std::vector<std::string>> joystick_names() {
        return local_manager->joystick_names();
    }

    std::shared_ptr<std::vector<SDL_JoystickID>> joystick_ids() {
        return local_manager->joystick_ids();
    }

    void change_device(int local_joystick_index) {
        local_manager->change_device(local_joystick_index);
    }

    void set_joystick_selection_preferences() {
        local_joystick->set_preferences_from_chosen_device();
    }

    SDL_JoystickID current_device_id() {
        return local_manager->device_id();
    }

    bool using_default_config() {
        return local_joystick->is_default_config();
    }

    bool can_use_preferences_config() {
        if(!local_joystick->get_device()) {
            return false;
        }
        return (preferences::configured_joystick_guid() == local_joystick->get_device()->get_guid());
    }

    void use_preferences_config() {
       local_joystick->configure_from_preferences();
    }

    void use_default_config() {
       local_joystick->configure_blind();
    } 

    //
    // Singular to player_controller calls.  See header and player_controller documentation.
    //

    void change_mapping(const int* kinds, const int* ids, const int* data0, const int* data1) {
        local_joystick->change_mapping(kinds, ids, data0, data1);
    }

    void set_joystick_configuration_preferences() {
        local_joystick->set_preferences_from_configuration();
    }


    bool up() {
        if(silent) {
            return false;
        } else {
            return local_joystick->up();
        }
    }

    bool down() {
        if(silent) {
            return false;
        } else {
            return local_joystick->down();
        }
    }

    bool left() {
        if(silent) {
            return false;
        } else {
        return local_joystick->left();
        }
    }

    bool right() {
        if(silent) {
            return false;
        } else {
        return local_joystick->right();
        }
    }

    bool button(int x) {
        if(silent) {
            return false;
        } else {
            return local_joystick->button(x);
        }
    } 



    //
    // Singular interface wrappers for interactive configurer.  See header file.
    //

    void start_configurer() {
        if(local_joystick && local_joystick->get_device()) {
            local_configurer = new interactive_controller_configurer(local_joystick->get_device());
        }
    }

    bool neutral_zones_known() {
        if(local_joystick) {
            return local_joystick->get_device()->know_neutral_points();
        }
        return false;
    }

    void clear_neutral_zones() {
        if(local_joystick && local_configurer) {
            local_configurer->clear_neutral_zones();
        }
    } 

    void examine_neutral_zones_tick() {
        if(local_joystick && local_configurer) {
            local_configurer->neutral_zones_tick();
        }
    }

    bool neutral_zones_dangerous() {
        if(local_joystick && local_configurer) {
            return local_configurer->neutral_zones_dangerous();
        }
        return false;
    }

    void default_neutral_zones() {
        if(local_joystick && local_configurer) {
            local_configurer->default_neutral_zones();
        }
    }
  
    listen_result listen_for_signal() {
        if(local_joystick && local_configurer) {
            return local_configurer->listen_for_signal();
        }
        return still_listening; 
    }

    bool retreat() {
        if(local_joystick && local_configurer) {
            return local_configurer->retreat();
        }
        return false;
    }
   
    void apply_configuration() {
        if(local_joystick && local_configurer) {
            local_joystick->change_mapping(
                local_configurer->get_part_kinds(),
                local_configurer->get_part_ids(),
                local_configurer->get_part_data0(),
                local_configurer->get_part_data1()
            );
        }
    }

    void stop_configurer() {
        delete local_configurer;
        local_configurer = NULL;
    }


    //
    // Initialisation and update
    //


    manager::manager() {
        local_joystick = NULL;
        local_configurer = NULL;
        local_manager = new joystick_manager();
        local_manager->initial_setup();
    }

    manager::~manager() {
        delete local_manager;
        haptic::get_effects().clear();
        haptic::haptic_devices.clear();

        // We rely on our shared_ptrs to various SDL data structures having
        // been deleted (and called the corresponding SDL_CloseThingy()
        // function) by the time we reach this point.
        std::cerr << "INFO: Shutting down SDL joystick, game controller and haptic modules... ";
        SDL_QuitSubSystem(SDL_INIT_HAPTIC | SDL_INIT_GAMECONTROLLER | SDL_INIT_JOYSTICK);
        std::cerr << " done." << std::endl;

#if defined(TARGET_BLACKBERRY)
        bps_shutdown();
#endif
    }

    bool pump_events(const SDL_Event& ev, bool claimed) {
        return local_manager->pump_events(ev, claimed);
    }


    // Update SDL's joystick statuses.  This will circulate input events as well.
    void update() {
        //if(preferences::use_joystick()) { //XXX Need to confirm safety of doing this.  
            SDL_JoystickUpdate();
        //}
    }



    int iphone_tilt() {

#if defined(TARGET_BLACKBERRY)
        double x, y, z;
        const int result = accelerometer_read_forces(&x, &y, &z);
        if(result != BPS_SUCCESS) {
            std::cerr << "READ OF ACCELEROMETER FAILED\n";
            return 0;
        } else {
            return x*1000;
        }
#endif

        return 0;
    }


}

namespace haptic {
	void play(const std::string& id, int iters)
	{
		for(auto hd : haptic_devices) {
			auto it = get_effects().find(hd.second.get());
			if(it != get_effects().end()) {
				auto idit = it->second.find(id);
				if(idit == it->second.end()) {
					SDL_HapticRumblePlay(hd.second.get(), 1.0, 750);
				} else {
					SDL_HapticRunEffect(hd.second.get(), idit->second, iters);
				}
			} else {
				SDL_HapticRumblePlay(hd.second.get(), 1.0, 750);
			}
		}
	}

	void stop(const std::string& id)
	{
		for(auto hd : haptic_devices) {
			auto it = get_effects().find(hd.second.get());
			auto idit = it->second.find(id);
			if(idit == it->second.end()) {
				SDL_HapticStopEffect(hd.second.get(), idit->second);
			}
		}
	}

	void stop_all()
	{
		for(auto hd : haptic_devices) {
			SDL_HapticStopAll(hd.second.get());
		}
	}

	HapticEffectCallable::HapticEffectCallable(const std::string& name, const variant& effect)
	{
		load(name, effect);
	}

	HapticEffectCallable::~HapticEffectCallable()
	{
	}

	namespace {
		void get_list3u(Uint16* li, const variant& v) {
			ASSERT_LOG(v.is_list(), "FATAL: Must be list type");
			for(size_t n = 0; n != 3 && n != v.num_elements(); ++n) {
				li[n] = Uint16(v[n].as_int());
			}
		}
		void get_list3s(Sint16* li, const variant& v) {
			ASSERT_LOG(v.is_list(), "FATAL: Must be list type");
			for(size_t n = 0; n != 3 && n != v.num_elements(); ++n) {
				li[n] = Sint16(v[n].as_int());
			}
		}
	}

	void HapticEffectCallable::load(const std::string& name, const variant& eff) {
		SDL_HapticEffect effect;
		SDL_memset(&effect, 0, sizeof(effect));

		// convert from our variant map to an SDL_HapticEffect structure.
		ASSERT_LOG(eff.has_key("type"), "FATAL: haptic effects must have 'type' key.");
		ASSERT_LOG(eff["type"].is_string(), "FATAL: 'type' key must be a string.");
		std::string type = eff["type"].as_string();

		Uint32 length = eff["length"].as_int();
		Uint16 delay = Uint16(eff["delay"].as_int());

		Uint16 button = 0;
		if(eff.has_key("button")) {
			button = Uint16(eff["button"].as_int());
		}
		Uint16 interval = 0;
		if(eff.has_key("interval")) {
			interval = Uint16(eff["interval"].as_int());
		}

		Uint16 attack_length = 0;
		if(eff.has_key("attack_length")) {
			attack_length = Uint16(eff["attack_length"].as_int());
		}
		Uint16 attack_level = 0;
		if(eff.has_key("attack_level")) {
			attack_level = Uint16(eff["attack_level"].as_int());
		}
		Uint16 fade_length = 0;
		if(eff.has_key("fade_length")) {
			fade_length = Uint16(eff["fade_length"].as_int());
		}
		Uint16 fade_level = 0;
		if(eff.has_key("fade_level")) {
			fade_level = Uint16(eff["fade_level"].as_int());
		}

		SDL_HapticDirection direction;
		if(eff.has_key("direction")) {
			const std::string& dir = eff["direction"].as_string();
			if(dir == "polar") {
				direction.type = SDL_HAPTIC_POLAR;
				direction.dir[0] =  eff["direction_rotation0"].as_int();
			} else if(dir == "cartesian") {
				direction.type = SDL_HAPTIC_CARTESIAN;
				direction.dir[0] =  eff["direction_x"].as_int();
				direction.dir[1] =  eff["direction_y"].as_int();
				if(eff.has_key("direction_z")) {
					direction.dir[2] =  eff["direction_z"].as_int();
				}
			} else if(dir == "sepherical") {
				direction.type = SDL_HAPTIC_SPHERICAL;
				direction.dir[0] =  eff["direction_rotation0"].as_int();
				if(eff.has_key("direction_rotation1")) {
					direction.dir[1] =  eff["direction_rotation1"].as_int();
				}
			} else {
				ASSERT_LOG(false, "FATAL: Unknown direction value '" << dir << "'");
			}
		}

		if(type == "constant") {
			effect.type = SDL_HAPTIC_CONSTANT;
			effect.constant.length = eff["level"].as_int();
			effect.constant.attack_length = attack_length;
			effect.constant.attack_level = attack_level;
			effect.constant.fade_length = fade_length;
			effect.constant.fade_level = fade_level;
			effect.constant.button = button;
			effect.constant.interval = interval;
			effect.constant.length = length;
			effect.constant.delay = delay;
		} else if(type == "sine" || type == "sqaure" || type == "triangle" || type == "sawtooth_up" || type == "sawtooth_down") {
			if(type == "sine") {
				effect.type = SDL_HAPTIC_SINE;
			//} else if(type == "sqaure") {
			//	effect.type = SDL_HAPTIC_SQUARE;
			} else if(type == "triangle") {
				effect.type = SDL_HAPTIC_TRIANGLE;
			} else if(type == "sawtooth_up") {
				effect.type = SDL_HAPTIC_SAWTOOTHUP;
			} else if(type == "sawtooth_down") {
				effect.type = SDL_HAPTIC_SAWTOOTHDOWN;
			}
			effect.periodic.period = eff["period"].as_int();
			effect.periodic.magnitude = eff["magnitude"].as_int();
			if(eff.has_key("offset")) {
				effect.periodic.offset = eff["offset"].as_int();
			}
			if(eff.has_key("phase")) {
				effect.periodic.phase = eff["phase"].as_int();
			}
			effect.periodic.attack_length = attack_length;
			effect.periodic.attack_level = attack_level;
			effect.periodic.fade_length = fade_length;
			effect.periodic.fade_level = fade_level;
			effect.periodic.button = button;
			effect.periodic.interval = interval;
			effect.periodic.length = length;
			effect.periodic.delay = delay;
		} else if(type == "spring" || type == "damper" || type == "inertia" || type == "friction") {
			if(type == "spring") {
				effect.type = SDL_HAPTIC_SPRING;
			} else if(type == "damper") {
				effect.type = SDL_HAPTIC_DAMPER;
			} else if(type == "inertia") {
				effect.type = SDL_HAPTIC_INERTIA;
			} else if(type == "friction") {
				effect.type = SDL_HAPTIC_FRICTION;
			}
			effect.condition.button = button;
			effect.condition.interval = interval;
			effect.condition.length = length;
			effect.condition.delay = delay;
			get_list3u(effect.condition.right_sat, eff["right_saturation"]);
			get_list3u(effect.condition.left_sat, eff["left_saturation"]);
			get_list3s(effect.condition.right_coeff, eff["right_coefficient"]);
			get_list3s(effect.condition.left_coeff, eff["left_coefficient"]);
			get_list3u(effect.condition.deadband, eff["deadband"]);
			get_list3s(effect.condition.center, eff["center"]);
		} else if(type == "ramp") {
			effect.type = SDL_HAPTIC_RAMP;
			effect.ramp.start = eff["start"].as_int();
			effect.ramp.start = eff["end"].as_int();
			effect.ramp.attack_length = attack_length;
			effect.ramp.attack_level = attack_level;
			effect.ramp.fade_length = fade_length;
			effect.ramp.fade_level = fade_level;
			effect.ramp.button = button;
			effect.ramp.interval = interval;
		} else if(type == "custom") {
			effect.type = SDL_HAPTIC_CUSTOM;
		}
		
		for(auto hd : haptic_devices) {
			int id = SDL_HapticNewEffect(hd.second.get(), &effect);
			if(id >= 0) {
				auto it = get_effects().find(hd.second.get());
				if(it != get_effects().end()) {
					it->second[name] = id;
				} else {
					std::map<std::string,int> m;
					m[name] = id;
					get_effects()[hd.second.get()] = m;
				}
			} else {
				std::cerr << "WARNING: error creating haptic effect(" << name << "): " << SDL_GetError() << std::endl;
			}
		}
	}

	BEGIN_DEFINE_CALLABLE_NOBASE(HapticEffectCallable)
		DEFINE_FIELD(dummy, "int")
		return variant(0);
	END_DEFINE_CALLABLE(HapticEffectCallable)
}
