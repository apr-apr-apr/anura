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
#ifndef DIALOG_HPP_INCLUDED
#define DIALOG_HPP_INCLUDED

#include "texture.hpp"
#include "widget.hpp"

#include <string>
#include <vector>

namespace gui {

std::string get_dialog_file(const std::string& fname);
void reset_dialog_paths();

class dialog : public widget
{
public:
	explicit dialog(int x, int y, int w, int h);
	explicit dialog(const variant& v, game_logic::formula_callable* e);
	virtual ~dialog();
	virtual void show_modal();
	void show();

	enum MOVE_DIRECTION { MOVE_DOWN, MOVE_RIGHT };
	dialog& add_widget(widget_ptr w, MOVE_DIRECTION dir=MOVE_DOWN);
	dialog& add_widget(widget_ptr w, int x, int y,
	                MOVE_DIRECTION dir=MOVE_DOWN);
	void remove_widget(widget_ptr w);
	void replace_widget(widget_ptr w_old, widget_ptr w_new);
	void clear();
	int padding() const { return padding_; }
	void set_padding(int pad) { padding_ = pad; }
	void close();
	void cancel() { cancelled_ = true; close(); }

	bool closed() { return !opened_; }
	bool cancelled() { return cancelled_; }
	void set_cursor(int x, int y) { add_x_ = x; add_y_ = y; }
	int cursor_x() const { return add_x_; }
	int cursor_y() const { return add_y_; }
    bool process_event(const SDL_Event& e, bool claimed);
	
	void set_on_quit(boost::function<void ()> onquit) { on_quit_ = onquit; }
	void set_on_close(boost::function<void (bool)> onclose) { on_close_ = onclose; }

	void set_background_frame(const std::string& id) { background_framed_gui_element_ = id; }
	void set_draw_background_fn(boost::function<void()> fn) { draw_background_fn_ = fn; }
	void set_upscale_frame(bool upscale=true) { upscale_frame_ = upscale; }

	virtual bool has_focus() const;
	void set_process_hook(boost::function<void()> fn) { on_process_ = fn; }
	static void draw_last_scene();
	virtual widget_ptr get_widget_by_id(const std::string& id);
	virtual const_widget_ptr get_widget_by_id(const std::string& id) const;

	void prepare_draw();
	void complete_draw();

	std::vector<widget_ptr> get_children() const;

	//add standardized okay/cancel buttons in the bottom right corner.
	void add_ok_and_cancel_buttons();

protected:
	virtual bool handle_event(const SDL_Event& event, bool claimed);
	virtual bool handle_event_children(const SDL_Event& event, bool claimed);
	virtual void handle_draw() const;
	virtual void handle_draw_children() const;
	void set_clear_bg(bool clear) { clear_bg_ = clear; };
	void set_clear_bg_amount(int amount) { clear_bg_ = amount; }
	int clear_bg() const { return clear_bg_; };

	bool pump_events();

	virtual void handle_process();
	void recalculate_dimensions();
private:
	DECLARE_CALLABLE(dialog);

	void do_up_event();
	void do_down_event();
	void do_select_event();
	
	sorted_widget_list widgets_;
	tab_sorted_widget_list tab_widgets_;
	int control_lockout_;

	tab_sorted_widget_list::iterator current_tab_focus_;

	bool opened_;
	bool cancelled_;
	int clear_bg_;
	
	boost::function<void()> on_quit_;
	boost::function<void(bool)> on_close_;

	void quit_delegate();
	void close_delegate(bool cancelled);

	game_logic::formula_ptr ffl_on_quit_;
	game_logic::formula_ptr ffl_on_close_;

	game_logic::formula_callable_ptr quit_arg_;
	game_logic::formula_callable_ptr close_arg_;

	//default padding between widgets
	int padding_;

	//where the next widget will be placed by default
	int add_x_, add_y_;

	graphics::texture bg_;
	mutable GLfloat bg_alpha_;

	int last_draw_;
	rect forced_dimensions_;

	std::string background_framed_gui_element_;
	boost::function<void()> draw_background_fn_;

	bool upscale_frame_;
};

typedef boost::intrusive_ptr<dialog> dialog_ptr;

}

#endif
