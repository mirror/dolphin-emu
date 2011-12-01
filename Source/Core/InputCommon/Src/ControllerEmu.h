// Copyright (C) 2010 Dolphin Project.

// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, version 2.0.

// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License 2.0 for more details.

// A copy of the GPL 2.0 should have been included with the program.
// If not, see http://www.gnu.org/licenses/

// Official SVN repository and contact information can be found at
// http://code.google.com/p/dolphin-emu/

#ifndef _CONTROLLEREMU_H_
#define _CONTROLLEREMU_H_

// windows crap
#define NOMINMAX

#include <cmath>
#include <vector>
#include <list>
#include <string>
#include <algorithm>

#include "GCPadStatus.h"

#include "ControllerInterface/ControllerInterface.h"
#include "IniFile.h"
#include "MathUtil.h"

#define sign(x) ((x)?(x)<0?-1:1:0)

enum
{
	GROUP_TYPE_OTHER,
	GROUP_TYPE_STICK,
	GROUP_TYPE_MIXED_TRIGGERS,
	GROUP_TYPE_BUTTONS,
	GROUP_TYPE_FORCE,
	GROUP_TYPE_EXTENSION,
	GROUP_TYPE_TILT,
	GROUP_TYPE_CURSOR,
	GROUP_TYPE_TRIGGERS,
	GROUP_TYPE_UDPWII,
	GROUP_TYPE_SLIDER,
};
enum
{
	SETTING_BACKGROUND_INPUT,
	SETTING_SIDEWAYS_WIIMOTE,
	SETTING_UPRIGHT_WIIMOTE,
	SETTING_MOTIONPLUS,
	SETTING_IR_HIDE,
};
enum
{
	AS_LEFT,
	AS_RIGHT,
	AS_UP,
	AS_DOWN,
	AS_MODIFIER,
};
enum
{
	AS_RADIUS,
	AS_DEADZONE,
	AS_SQUARE,
};
enum
{
	B_NR_THRESHOLD,
};
enum
{
	B_RANGE,
	B_THRESHOLD,
};
enum
{
	F_FORWARD,
	F_BACKWARD,
	F_LEFT,
	F_RIGHT,
	F_UP,
	F_DOWN,
};
enum
{
	F_RANGE,
	F_DEADZONE,
};
enum
{
	R_FORWARD,
	R_BACKWARD,
	R_LEFT,
	R_RIGHT,
	R_UP,
	R_DOWN,
	R_BASE_MODIFIER,
	R_RANGE_MODIFIER,
};
enum
{
	R_ACC_RANGE,
	R_GYRO_RANGE,
	R_DEADZONE,
	R_CIRCLESTICK,
};
enum
{
	R_NG_RANGE,
	R_NG_DEADZONE,
	R_NG_CIRCLESTICK,
};
enum
{
	C_UP,
	C_DOWN,
	C_LEFT,
	C_RIGHT,
	C_FORWARD,
	C_BACKWARD,
	C_MODIFIER,
	C_HIDE,
	C_SHOW,
};
enum
{
	C_IR_SENSITIVITY,
	C_GYRO_SENSITIVITY,
	C_CENTER,
	C_WIDTH,
	C_HEIGHT,
};

const char * const named_directions[] = 
{
	"Up",
	"Down",
	"Left",
	"Right"
};

class ControllerEmu
{
public:

	class ControlGroup
	{
	public:

		class Control
		{
		protected:
			Control(ControllerInterface::ControlReference* const _ref, const char * const _name)
				: control_ref(_ref), name(_name){}
		public:

			virtual ~Control();
			ControllerInterface::ControlReference*		const control_ref;
			const char * const		name;

		};

		class Input : public Control
		{
		public:

			Input(const char * const _name)
				: Control(new ControllerInterface::InputReference, _name) {}

		};

		class Output : public Control
		{
		public:

			Output(const char * const _name)
				: Control(new ControllerInterface::OutputReference, _name) {}

		};

		class Setting
		{
		public:

			Setting(const char* const _name, const ControlState def_value
				, const unsigned int _low = 0, const unsigned int _high = 100 )
				: name(_name)
				, value(def_value)
				, default_value(def_value)
				, low(_low)
				, high(_high){}

			const char* const	name;
			ControlState		value;
			const ControlState	default_value;
			const unsigned int	low, high;
		};

		ControlGroup(const char* const _name, const unsigned int _type = GROUP_TYPE_OTHER) : name(_name), type(_type) {}
		virtual ~ControlGroup();
	
		virtual void LoadConfig(IniFile::Section *sec, const std::string& defdev = "", const std::string& base = "" );
		virtual void SaveConfig(IniFile::Section *sec, const std::string& defdev = "", const std::string& base = "" );

		const char* const			name;
		const unsigned int			type;

		std::vector< Control* >		controls;
		std::vector< Setting* >		settings;

	};

	class AnalogStick : public ControlGroup
	{
	public:

		template <typename C>
		void GetState(C* const x, C* const y, const unsigned int base, const unsigned int range)
		{
			// this is all a mess

			ControlState yy = controls[0]->control_ref->State() - controls[1]->control_ref->State();
			ControlState xx = controls[3]->control_ref->State() - controls[2]->control_ref->State();

			ControlState radius = settings[AS_RADIUS]->value;
			ControlState deadzone = settings[AS_DEADZONE]->value;
			ControlState square = settings[AS_SQUARE]->value;
			ControlState m = controls[AS_MODIFIER]->control_ref->State();

			// modifier code
			if (m)
			{
				yy = (fabsf(yy)>deadzone) * sign(yy) * (m + deadzone/2);
				xx = (fabsf(xx)>deadzone) * sign(xx) * (m + deadzone/2);
			}

			// deadzone / square stick code
			if (radius != 1 || deadzone || square)
			{
				// this section might be all wrong, but its working good enough, i think

				ControlState ang = atan2(yy, xx);
				ControlState ang_sin = sin(ang);
				ControlState ang_cos = cos(ang);
				ControlState rad = sqrt(xx*xx + yy*yy);

				// the amt a full square stick would have at current angle
				ControlState square_full = std::min(ang_sin ? 1/fabsf(ang_sin) : 2, ang_cos ? 1/fabsf(ang_cos) : 2);

				// the amt a full stick would have that was ( user setting squareness) at current angle
				// i think this is more like a pointed circle rather than a rounded square like it should be
				ControlState stick_full = (1 + (square_full - 1) * square);

				// radius
				rad *= radius;

				// dead zone code
				rad = std::max(0.0f, rad - deadzone * stick_full);
				rad /= (1 - deadzone);

				// square stick code
				ControlState amt = rad / stick_full;
				rad -= ((square_full - 1) * amt * square);

				yy = std::max(-1.0f, std::min(1.0f, ang_sin * rad));
				xx = std::max(-1.0f, std::min(1.0f, ang_cos * rad));
			}

			*y = C(yy * range + base);
			*x = C(xx * range + base);
		}

		AnalogStick(const char* const _name);

	};

	class Buttons : public ControlGroup
	{
	public:
		Buttons(const char* const _name, bool range = false);

		template <typename C>
		void GetState(C* const buttons, const C* bitmasks, bool unset = false)
		{
			std::vector<Control*>::iterator i = controls.begin(),
				e = controls.end();
			for (; i!=e; ++i, ++bitmasks)
				if ((*i)->control_ref->State() > settings[m_range?B_THRESHOLD:B_NR_THRESHOLD]->value) // threshold
					unset ? *buttons &= ~*bitmasks : *buttons |= *bitmasks;
		}
	private:
		bool	m_range;
	};

	class MixedTriggers : public ControlGroup
	{
	public:

		template <typename C, typename S>
		void GetState(C* const digital, const C* bitmasks, S* analog, const unsigned int range)
		{
			const unsigned int trig_count = ((unsigned int) (controls.size() / 2));
			for (unsigned int i=0; i<trig_count; ++i,++bitmasks,++analog)
			{
				if (controls[i]->control_ref->State() > settings[0]->value) //threshold
				{
					*analog = range;
					*digital |= *bitmasks;
				}
				else
					*analog = S(controls[i+trig_count]->control_ref->State() * range);
					
			}
		}

		MixedTriggers(const char* const _name);

	};

	class Triggers : public ControlGroup
	{
	public:

		template <typename S>
		void GetState(S* analog, const unsigned int range)
		{
			const unsigned int trig_count = ((unsigned int) (controls.size()));
			const ControlState deadzone = settings[0]->value;
			for (unsigned int i=0; i<trig_count; ++i,++analog)
				*analog = S(std::max(controls[i]->control_ref->State() - deadzone, 0.0f) / (1 - deadzone) * range);
		}

		Triggers(const char* const _name);

	};

	class Slider : public ControlGroup
	{
	public:

		template <typename S>
		void GetState(S* const slider, const unsigned int range, const unsigned int base = 0)
		{
			const float deadzone = settings[0]->value;
			const float state = controls[1]->control_ref->State() - controls[0]->control_ref->State();

			if (fabsf(state) > deadzone)
				*slider = (S)((state - (deadzone * sign(state))) / (1 - deadzone) * range + base);
			else
				*slider = 0;
		}

		Slider(const char* const _name);

	};

	class Force : public ControlGroup
	{
	public:
		Force(const char* const _name);

		template <typename C, typename R>
		void GetState(C* axis, const u8 base, const R range, bool step = true)
		{
			const float master_range = settings[F_RANGE]->value;
			const float deadzone = settings[F_DEADZONE]->value;

			for (unsigned int i=0; i<6; i+=2)
			{
				float dz = 0;
				ControlState state = controls[i == 0 ? F_FORWARD : (i == 2 ? F_LEFT : F_UP)]->control_ref->State() - controls[i == 0 ? F_BACKWARD : (i == 2 ? F_RIGHT : F_DOWN)]->control_ref->State();				

				if (fabsf(state) > deadzone)
					dz = ((state - (deadzone * sign(state))) / (1 - deadzone));

				if (step) {
					if (state > m_thrust[i>>1])
						m_thrust[i>>1] = std::min(m_thrust[i>>1] + 0.1f, state);
					else if (state < m_thrust[i>>1])
						m_thrust[i>>1] = std::max(m_thrust[i>>1] - 0.1f, state);
				}

				*axis++ = (C)((abs(m_thrust[i>>1]) >= 0.7 ? -2*sign(state)+m_thrust[i>>1]*2 : m_thrust[i>>1]) * sign(state) * range * master_range + base);
			}
		}
	private:
		float	m_thrust[3];
	};

	class Rotate : public ControlGroup
	{
	public:
		Rotate(const char* const _name, bool gyro = false);

		template <typename C, typename R>
		void GetState(C* const x, C* const y, C* const z, const unsigned int base, const R range, const bool step = true)
		{
			ControlState xx = controls[R_RIGHT]->control_ref->State() - controls[R_LEFT]->control_ref->State();
			ControlState yy = controls[R_FORWARD]->control_ref->State() - controls[R_BACKWARD]->control_ref->State();			
			ControlState zz = controls[R_DOWN]->control_ref->State() - controls[R_UP]->control_ref->State();

			ControlState deadzone = settings[m_gyro?R_DEADZONE:R_NG_DEADZONE]->value;
			ControlState circle = settings[m_gyro?R_CIRCLESTICK:R_NG_CIRCLESTICK]->value;
			ControlState b = controls[R_BASE_MODIFIER]->control_ref->State();
			ControlState r = controls[R_RANGE_MODIFIER]->control_ref->State();
			//SWARN_LOG(CONSOLE, "1 | %5.2f %5.2f %5.2f", xx, yy, zz);
			// deadzone
			xx *= fabsf(xx)>deadzone ? 1.0 : 0.0;
			yy *= fabsf(yy)>deadzone ? 1.0 : 0.0;
			zz *= fabsf(zz)>deadzone ? 1.0 : 0.0;
			//SWARN_LOG(CONSOLE, "2 | %5.2f %5.2f %5.2f", xx, yy, zz);
			// circle stick
			if (circle)
			{
				ControlState ang = atan2(yy, xx);
				ControlState ang_sin = sin(ang);
				ControlState ang_cos = cos(ang);
				ControlState rad = sqrt(xx*xx + yy*yy);

				// the amt a full square stick would have at current angle
				ControlState square_full = std::min(ang_sin ? 1/fabsf(ang_sin) : 2, ang_cos ? 1/fabsf(ang_cos) : 2);

				// the amt a full stick would have that was (user setting circular) at current angle
				// i think this is more like a pointed circle rather than a rounded square like it should be
				ControlState stick_full = (square_full * (1 - circle)) + (circle);				

				// dead zone
				rad = std::max(0.0f, rad - deadzone * stick_full);
				rad /= (1 - deadzone);

				// circle stick
				ControlState amt = rad / stick_full;
				rad += (square_full - 1) * amt * circle;

				yy = ang_sin * rad;
				xx = ang_cos * rad;
				zz *= (fabsf(zz)>deadzone);
			}
			//SWARN_LOG(CONSOLE, "3 | %5.2f %5.2f %5.2f", xx, yy, zz);
			// step towards new value
			if (step)
			{
				if (xx > m_rotate[0])
					m_rotate[0] = std::min(m_rotate[0] + 0.1f, xx);
				else if (xx < m_rotate[0])
					m_rotate[0] = std::max(m_rotate[0] - 0.1f, xx);

				if (yy > m_rotate[1])
					m_rotate[1] = std::min(m_rotate[1] + 0.1f, yy);
				else if (yy < m_rotate[1])
					m_rotate[1] = std::max(m_rotate[1] - 0.1f, yy);

				if (zz > m_rotate[2])
					m_rotate[2] = std::min(m_rotate[2] + 0.1f, zz);
				else if (zz < m_rotate[2])
					m_rotate[2] = std::max(m_rotate[2] - 0.1f, zz);
			}

			//SWARN_LOG(CONSOLE, "4 | %5.2f %5.2f %5.2f | %5.2f %5.2f %5.2f", xx, yy, zz, b, r, range);
			*x = C(m_rotate[0] * range * (r ? r : 1.0) + base + sign(m_rotate[0])*b);
			*y = C(m_rotate[1] * range * (r ? r : 1.0) + base + sign(m_rotate[1])*b);
			*z = C(m_rotate[2] * range * (r ? r : 1.0) + base + sign(m_rotate[2])*b);
			//SWARN_LOG(CONSOLE, "5 | %5.2f %5.2f %5.2f | %5.2f %5.2f %5.2f\n", *x, *y, *z, b, r, range);
		}
	private:
		float	m_rotate[3];
		bool	m_gyro;
	};

	class Cursor : public ControlGroup
	{
	public:
		Cursor(const char* const _name);

		template <typename C, typename R>
		void GetState(C* const x, C* const y, C* const z, const R range = 1.0, const bool adjusted = true, const bool relative = false, const bool step = false)
		{
			C* axis;
			ControlState m = controls[C_MODIFIER]->control_ref->State();
			for (unsigned int i=0; i<6; i+=2)
			{
				ControlState state = controls[i == 0 ? C_UP : (i == 2 ? C_LEFT : C_FORWARD)]->control_ref->State();
				const bool is_relative = controls[i == 0 ? C_UP : (i == 2 ? C_LEFT : C_FORWARD)]->control_ref->IsRelative();
				
				// hide
				if (controls[C_HIDE]->control_ref->State() > 0.5f)
				{
					m_state[0] = 0;
					m_state[1] = 10000;
					m_state[2] = 0;
				}
				// relative input
				else if (is_relative)
				{
					// moving average smooth input
					float fsum = 0;
					m_list.at(i>>1).push_back((float)state);
					for (std::list<float>::iterator it = m_list.at(i>>1).begin(); it != m_list.at(i>>1).end(); it++)
						fsum += *it;
					if (m_list.at(i>>1).size() > 0) state = fsum / (float)m_list.at(i>>1).size();
					if (m_list.at(i>>1).size() >= (relative ? 10 : 1)) m_list.at(i>>1).pop_front();
					if (m_list.at(i>>1).size() >= 10) m_list.at(i>>1).resize(9);
					
					// create absolut data
					if (!relative)
					{
						if (!step) { m_state[i>>1] = m_absolute[i>>1]; continue; }
						state *= range * 0.005 * (m ? m : 1.0);
						m_absolute[i>>1] += (i == 0 ? -state : state);
						m_absolute[i>>1] = MathUtil::Trim(m_absolute[i>>1], -1, 1);
						m_state[i>>1] = m_absolute[i>>1];

						// sync with system cursor
						//if(!(mouse_x == last_mouse_x && mouse_y == last_mouse_y)) {
						//	RECT r;
						//	GetWindowRect(hwnd, &r);
						//	// force default cursor
						//	ShowCursor(1);
						//	// windowed mode adjustments
						//	if(r.top != 0 && !SConfig::GetInstance().m_LocalCoreStartupParameter.bRenderToMain) {
						//		r.top += 32;
						//		r.top += 0;
						//		r.left += 4;
						//		r.right -= 4;
						//		r.bottom -= 4;
						//	}
						//		SetCursorPos(r.left + mouse_x,r.top + mouse_y);
						//	}
						//}
						//last_mouse_x = mouse_x; last_mouse_y = mouse_y;
					}
					// return raw input
					else
					{
						state *= range * 0.005 * (m ? m : 1.0);
						m_state[i>>1] = state;
						//if (relative) SWARN_LOG(CONSOLE, "%d %d | %d %5.2f %5.2f", i, is_relative, relative, state, m_state[i>>1]);
					}
				}
				else
				{
					ControlState state = controls[i == 0 ? C_UP : (i == 2 ? C_RIGHT : C_BACKWARD)]->control_ref->State() - controls[i == 0 ? C_DOWN : (i == 2 ? C_LEFT : C_FORWARD)]->control_ref->State();

					if (!relative)
					{							
						m_state[i>>1] = std::min(1.0f, state);
					}
					// create relative data
					else
					{
						m_state[i>>1] = state - m_last[i>>1];
						m_last[i>>1] = state;
					}
				}
			}

			*y = m_state[0];
			*x = m_state[1];
			*z = m_state[2];

			// adjust absolute cursor
			if (adjusted && !relative)
			{
				*x *= (settings[C_WIDTH]->value * 2.0);
				*y *= (settings[C_HEIGHT]->value * 2.0);
				*y += (settings[C_CENTER]->value - 0.5f);
			}
		}

		float							m_state[3], m_absolute[3], m_last[3];
		std::vector<std::list<float> >	m_list;
	};

	class Extension : public ControlGroup
	{
	public:
		Extension(const char* const _name)
			: ControlGroup(_name, GROUP_TYPE_EXTENSION)
			, switch_extension(0)
			, active_extension(0) {}
		~Extension();

		void GetState(u8* const data, const bool focus = true);

		std::vector<ControllerEmu*>		attachments;

		int	switch_extension;
		int	active_extension;
	};

	virtual ~ControllerEmu();

	virtual std::string GetName() const = 0;

	virtual void LoadDefaults(const ControllerInterface& ciface);

	virtual void LoadConfig(IniFile::Section *sec, const std::string& base = "");
	virtual void SaveConfig(IniFile::Section *sec, const std::string& base = "");
	void UpdateDefaultDevice();

	void UpdateReferences(ControllerInterface& devi);

	std::vector< ControlGroup* >		groups;

	ControllerInterface::DeviceQualifier	default_device;

};


#endif