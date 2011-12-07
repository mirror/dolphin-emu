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
#include "Timer.h"

#define sign(x) ((x)?(x)<0?-1:1:0)

enum
{
	GROUP_TYPE_OTHER,
	GROUP_TYPE_STICK,
	GROUP_TYPE_MIXED_TRIGGERS,
	GROUP_TYPE_BUTTONS,
	GROUP_TYPE_FORCE,
	GROUP_TYPE_EXTENSION,
	GROUP_TYPE_ROTATE,
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
};
enum
{
	R_RANGE_MODIFIER = 6,
};
enum
{
	R_RANGE,
	R_DEADZONE,
	R_CIRCLESTICK,
};
enum
{
	R_G_FAST_MODIFIER = 6,
	R_G_ACC_RANGE_MODIFIER,
	R_G_GYRO_RANGE_MODIFIER_1,
	R_G_GYRO_RANGE_MODIFIER_2,
};
enum
{
	R_G_ACC_RANGE,
	R_G_GYRO_RANGE,
	R_G_GYRO_SETTLE,
	R_G_DEADZONE,
	R_G_CIRCLESTICK,
};
enum
{
	C_UP,
	C_DOWN,
	C_LEFT,
	C_RIGHT,
	C_FORWARD,
	C_BACKWARD,
	C_FAST_MODIFIER,
	C_RANGE_MODIFIER,
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

				if (step)
				{
					if (state > m_thrust[i>>1])
						m_thrust[i>>1] = std::min(m_thrust[i>>1] + 0.1f, state);
					else if (state < m_thrust[i>>1])
						m_thrust[i>>1] = std::max(m_thrust[i>>1] - 0.1f, state);
				}

				// deceleration switch
				m_state[i>>1] = (abs(m_thrust[i>>1]) >= 0.7 ? -2*sign(state)+m_thrust[i>>1]*2 : m_thrust[i>>1])*sign(state);
				//m_state[i>>1] = m_thrust[i>>1];

				*axis++ = (C)(m_state[i>>1] * range * master_range + base);
			}
		}
	private:
		float	m_thrust[3], m_state[3];
	};

	class Rotate : public ControlGroup
	{
	public:
		Rotate(const char* const _name, bool gyro = false);

		bool HasGyro() { return m_has_gyro; }

		template <typename C, typename R>
		void GetState(C* const pitch, C* const roll, C* const yaw, const bool gyro = false, const unsigned int base = 0.0, const R range = 1.0, const bool step = true)
		{
			ControlState deadzone = settings[m_has_gyro ? R_G_DEADZONE : R_DEADZONE]->value;
			ControlState circle = settings[m_has_gyro ? R_G_CIRCLESTICK : R_CIRCLESTICK]->value;
			ControlState settle = 0; if (m_has_gyro) settle = settings[R_G_GYRO_SETTLE]->value;
			ControlState r_acc = controls[m_has_gyro ? R_G_ACC_RANGE_MODIFIER : R_RANGE_MODIFIER]->control_ref->State();
			ControlState r_gyro_1 = 1.0; if (m_has_gyro) r_gyro_1 = controls[R_G_GYRO_RANGE_MODIFIER_1]->control_ref->State();
			ControlState r_gyro_2 = 1.0; if (m_has_gyro) r_gyro_2 = controls[R_G_GYRO_RANGE_MODIFIER_2]->control_ref->State();

			for (unsigned int i=0; i<3; i++)
			{
				ControlState state = controls[i == 0 ? R_FORWARD : (i == 1 ? R_RIGHT : R_DOWN)]->control_ref->State() - controls[i == 0 ? R_BACKWARD : (i == 1 ? R_LEFT : R_UP)]->control_ref->State();		

				//SWARN_LOG(CONSOLE, "1 | %5.2f %5.2f %5.2f", xx, yy, zz);
				// deadzone
				state *= fabsf(state)>deadzone ? (fabsf(state)-deadzone)/(1.0-deadzone) : 0.0;
				//SWARN_LOG(CONSOLE, "2 | %5.2f %5.2f %5.2f", xx, yy, zz);
				if (step)
				{
					if (state == m_acc[i]) m_settle[i]++; else m_settle[i] = 0;
					// step towards state
					if (state > m_acc[i])
						m_acc[i] = std::min(m_acc[i] + 0.1f, state);
					else if (state < m_acc[i])
						m_acc[i] = std::max(m_acc[i] - 0.1f, state);
					// step gyro towards 0
					if (m_settle[i] > settle*100.0)
					{
						if (0 > m_gyro[i])
							m_gyro[i] = std::min(m_gyro[i] + 0.05f, 0.0f);
						else if (0 < m_gyro[i])
							m_gyro[i] = std::max(m_gyro[i] - 0.05f, 0.0f);
					}
					// step gyro towards state
					else
					{
						if (m_acc[i] > m_gyro[i])
							m_gyro[i] = std::min(m_gyro[i] + 0.05f, m_acc[i]);
						else if (m_acc[i] < m_gyro[i])
							m_gyro[i] = std::max(m_gyro[i] - 0.05f, m_acc[i]);
					}
					//if (m_has_gyro) SWARN_LOG(CONSOLE, "Center: %d %5.2f %5d %5d | %5.2f %5.2f", i, state, int(settle*100.0), m_settle[i], m_acc[i], m_gyro[i])
				}
			}

			ControlState y = m_acc[0];
			ControlState x = m_acc[1];

			// circle stick
			if (circle)
			{
				ControlState ang = atan2(y, x);
				ControlState ang_sin = sin(ang);
				ControlState ang_cos = cos(ang);
				ControlState rad = sqrt(x*x + y*y);

				// the amt a full square stick would have at current angle
				ControlState square_full = std::min(ang_sin ? 1/fabsf(ang_sin) : 2, ang_cos ? 1/fabsf(ang_cos) : 2);

				// the amt a full stick would have that was (user setting circular) at current angle
				// i think this is more like a pointed circle rather than a rounded square like it should be
				ControlState stick_full = (square_full * (1 - circle)) + (circle);				

				// dead zone
				rad = std::max(0.0f, rad - deadzone * stick_full);
				rad /= (1.0 - deadzone);

				// circle stick
				ControlState amt = rad / stick_full;
				rad += (square_full - 1) * amt * circle;

				y = ang_sin * rad;
				x = ang_cos * rad;
			}

			m_acc[0] = y;
			m_acc[1] = x;

			//SWARN_LOG(CONSOLE, "4 | %5.2f %5.2f %5.2f | %5.2f %5.2f %5.2f", xx, yy, zz, b, r, range);
			if (gyro)
			{
				*pitch = C(m_gyro[0] * range * (r_gyro_1 ? r_gyro_1 : 1.0) * (r_gyro_2 ? r_gyro_2 : 1.0) + base);
				*roll = C(m_gyro[1] * range * (r_gyro_1 ? r_gyro_1 : 1.0) * (r_gyro_2 ? r_gyro_2 : 1.0) + base);
				*yaw = C(m_gyro[2] * range * (r_gyro_1 ? r_gyro_1 : 1.0) * (r_gyro_2 ? r_gyro_2 : 1.0) + base);
				//SERROR_LOG(CONSOLE, "4 | %5.2f %5.2f %5.2f | %d %5.2f %5.2f %5.2f", m_gyro[0], m_gyro[1], m_gyro[2], base, range, r_gyro_1, r_gyro_2);
			}
			else
			{
				*pitch = C(m_acc[0] * range * (r_acc ? r_acc : 1.0) + base);
				*roll = C(m_acc[1] * range * (r_acc ? r_acc : 1.0) + base);
				*yaw = C(m_acc[2] * range * (r_acc ? r_acc : 1.0) + base);	
			}
			//SWARN_LOG(CONSOLE, "5 | %5.2f %5.2f %5.2f | %5.2f %5.2f %5.2f\n", *x, *y, *z, b, r, range);
		}
	private:
		float	m_acc[3], m_gyro[3];
		int		m_settle[3];
		bool	m_has_gyro;
	};

	class Cursor : public ControlGroup
	{
	public:
		Cursor(const char* const _name);

		template <typename C, typename R>
		void GetState(C* const x, C* const y, C* const z, const R range = 1.0, const bool adjusted = true, const bool relative = false, const bool step = false)
		{
			ControlState r = controls[C_RANGE_MODIFIER]->control_ref->State();

			for (unsigned int i=0; i<3; i++)
			{
				ControlState state = controls[i == 0 ? C_UP : (i == 1 ? C_LEFT : C_FORWARD)]->control_ref->State();
				const bool is_relative = controls[i == 0 ? C_UP : (i == 1 ? C_LEFT : C_FORWARD)]->control_ref->IsRelative();
				
				// hide
				if (controls[C_HIDE]->control_ref->State() > 0.5f)
				{
					m_state[0] = 0;
					if (relative) m_state[1] = 0; else m_state[1] = 10000; // what is the 10000 for?		
					m_state[2] = 0;		
				}
				// relative input
				else if (is_relative)
				{
					// moving average smooth input					
					if (step) m_list[i].push_back(state);
					float fsum = 0;  int j = 0;
					if (m_list[i].size() > 0)
					{
						for (std::list<float>::reverse_iterator it = m_list[i].rbegin(); relative ? it != m_list[i].rend() : j < 1; it++, j++)
							fsum += *it;
						state = fsum / float(m_list[i].size());
					}
					if (step) if (m_list[i].size() >= 10) m_list[i].pop_front();
					
					// create absolute data
					if (!relative)
					{						
						if (!step)
						{
							// absolute position not stepped anymore, reset
							if (m_absolute[i] != 0 && m_timer.GetTimeDifference() > 1000)
							{
								if (m_absolute[i] == m_absolute_last[i]) m_absolute[i] = 0;
								m_absolute_last[i] = m_absolute[i];
								m_timer.Update();
							}
							m_state[i] = m_absolute[i];
							continue;
						}
						// update absolute
						state *= range * 0.01 * (r ? r : 1.0);
						m_absolute[i] += (i == 0 ? -state : state);
						m_absolute[i] = MathUtil::Trim(m_absolute[i], -1, 1);
						m_state[i] = m_absolute[i];

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
						state *= range * 0.005 * (r ? r : 1.0);
						m_state[i] = state;
						//if (relative) SWARN_LOG(CONSOLE, "%d %d | %d %5.2f %5.2f", i, is_relative, relative, state, m_state[i]);
					}
				}
				else
				{
					ControlState state = controls[i == 0 ? C_UP : (i == 1 ? C_RIGHT : C_BACKWARD)]->control_ref->State() - controls[i == 0 ? C_DOWN : (i == 1 ? C_LEFT : C_FORWARD)]->control_ref->State();

					if (!relative)
					{							
						m_state[i] = std::min(1.0f, state);
					}
					// create relative data
					else
					{
						m_state[i] = state - m_last[i];
						m_last[i] = state;
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

		float				m_state[3], m_absolute[3], m_absolute_last[3], m_last[3];
		std::list<float>	m_list[3];
		Common::Timer		m_timer;
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