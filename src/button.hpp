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
#ifndef BUTTON_HPP_INCLUDED
#define BUTTON_HPP_INCLUDED

#include <boost/function.hpp>
#include <boost/scoped_ptr.hpp>

#include "texture.hpp"
#include "widget.hpp"
#include "framed_gui_element.hpp"

namespace gui {

enum BUTTON_RESOLUTION { BUTTON_SIZE_NORMAL_RESOLUTION, BUTTON_SIZE_DOUBLE_RESOLUTION };
enum BUTTON_STYLE { BUTTON_STYLE_NORMAL, BUTTON_STYLE_DEFAULT };	//"default" means a visually fat-edged button - the one that gets pressed by hitting enter.  This is standard gui lingo, it's what the dialogue "defaults" to doing when you press return.

//a button widget. Forwards to a given function whenever it is clicked.
class button : public widget
{
public:
	struct SetColorSchemeScope {
		explicit SetColorSchemeScope(variant v);
		~SetColorSchemeScope();
		variant backup;
	};

	button(const std::string& label, boost::function<void ()> onclick);
	button(widget_ptr label, boost::function<void ()> onclick, BUTTON_STYLE button_style = BUTTON_STYLE_NORMAL, BUTTON_RESOLUTION button_resolution = BUTTON_SIZE_NORMAL_RESOLUTION);
	button(const variant& v, game_logic::formula_callable* e);
	void set_color_scheme(const variant& v);
	virtual widget_ptr get_widget_by_id(const std::string& id);
	virtual const_widget_ptr get_widget_by_id(const std::string& id) const;
	void set_click_handler(boost::function<void ()> click_fun) { onclick_ = click_fun; }

	void set_hpadding(int hpad);
	void set_vpadding(int vpad);

	virtual void set_focus(bool f=true);

	virtual void do_execute();

	std::vector<widget_ptr> get_children() const;
protected:
	void set_label(widget_ptr label);
	virtual void handle_process();
	virtual variant handle_write();
	BUTTON_RESOLUTION button_resolution() const { return button_resolution_; }
	virtual widget_settings_dialog* settings_dialog(int x, int y, int w, int h);

	DECLARE_CALLABLE(button);
private:
	virtual void visit_values(game_logic::formula_callable_visitor& visitor);

	void setup();

	void handle_draw() const;
	bool handle_event(const SDL_Event& event, bool claimed);
	void click();
	int vpadding_;
	int hpadding_;

	BUTTON_RESOLUTION button_resolution_;
	BUTTON_STYLE button_style_;
	widget_ptr label_;
	boost::function<void ()> onclick_;
	bool down_;
	game_logic::formula_ptr click_handler_;
	game_logic::formula_callable_ptr handler_arg_;
	
	const_framed_gui_element_ptr normal_button_image_set_,depressed_button_image_set_,focus_button_image_set_,current_button_image_set_;

	boost::scoped_ptr<graphics::color> normal_color_, depressed_color_, focus_color_;
	boost::scoped_ptr<graphics::color> text_normal_color_, text_depressed_color_, text_focus_color_;
};

typedef boost::intrusive_ptr<button> button_ptr;

}

#endif
