// Copyright 2013 Dolphin Emulator Project
// Licensed under GPLv2
// Refer to the license.txt file included.

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
	GROUP_TYPE_OPTIONS,
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
	SETTING_MOTIONPLUS_FAST,
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
	B_THRESHOLD,
};
enum
{
	B_RANGE,
	B_THRESHOLD_R,
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
	T_FORWARD,
	T_BACKWARD,
	T_LEFT,
	T_RIGHT,
	T_UP,
	T_DOWN,
};
enum
{
	T_FAST = 6,
	T_ACC_RANGE,
	T_GYRO_RANGE_1,
	T_GYRO_RANGE_2,
};
enum
{
	T_MODIFIER = 6,
};
enum
{
	T_ACC_RANGE_S,
	T_GYRO_RANGE,
	T_GYRO_SETTLE,
	T_DEADZONE,
	T_CIRCLESTICK,
	T_ANGLE,
};
enum
{
	T_RANGE,
	T_DEADZONE_N,
	T_CIRCLESTICK_N,
	T_ANGLE_N,
};

enum
{
	C_UP,
	C_DOWN,
	C_LEFT,
	C_RIGHT,
	C_FORWARD,
	C_BACKWARD,
	C_RANGE_MODIFIER,
	C_HIDE,
	C_SHOW,
};
enum
{
	C_RANGE,
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
				, const unsigned int _low = 0, const unsigned int _high = 100)
				: name(_name)
				, value(def_value)
				, default_value(def_value)
				, low(_low)
				, high(_high)
				, lastState(false)
			{
				control = new Input("");
			}
			void GetState();

			const char* const	name;
			ControlState		value;
			const ControlState	default_value;
			const unsigned int	low, high;
			Control*			control;
			bool				lastState;
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
				// this section might be all wrong, but its working good enough, I think

				ControlState ang = atan2(yy, xx);
				ControlState ang_sin = sin(ang);
				ControlState ang_cos = cos(ang);

				// the amt a full square stick would have at current angle
				ControlState square_full = std::min(ang_sin ? 1/fabsf(ang_sin) : 2, ang_cos ? 1/fabsf(ang_cos) : 2);

				// the amt a full stick would have that was ( user setting squareness) at current angle
				// I think this is more like a pointed circle rather than a rounded square like it should be
				ControlState stick_full = (1 + (square_full - 1) * square);

				ControlState dist = sqrt(xx*xx + yy*yy);

				// dead zone code
				dist = std::max(0.0f, dist - deadzone * stick_full);
				dist /= (1 - deadzone);

				// square stick code
				ControlState amt = dist / stick_full;
				dist -= ((square_full - 1) * amt * square);

				// radius
				dist *= radius;

				yy = std::max(-1.0f, std::min(1.0f, ang_sin * dist));
				xx = std::max(-1.0f, std::min(1.0f, ang_cos * dist));
			}

			*y = C(yy * range + base);
			*x = C(xx * range + base);
		}

		AnalogStick(const char* const _name);

	};

	class Buttons : public ControlGroup
	{
	public:
		Buttons(const char* const _name, bool has_range = false);

		bool HasRange() { return m_has_range; }

		template <typename C>
		void GetState(C* const buttons, const C* bitmasks)
		{
			std::vector<Control*>::iterator
				i = controls.begin(),
				e = controls.end();

			for (; i!=e; ++i, ++bitmasks)
			{
				if ((*i)->control_ref->State() > settings[m_has_range ? B_THRESHOLD_R : B_THRESHOLD]->value) // threshold
					*buttons |= *bitmasks;
			}
		}

		void GetState(ControlState* buttons)
		{
			auto range = settings[B_RANGE]->value;

			std::vector<Control*>::iterator
				i = controls.begin(),
				e = controls.end();

			for (; i!=e; ++i)
			{
				if ((*i)->control_ref->State() > settings[m_has_range ? B_THRESHOLD_R : B_THRESHOLD]->value)
					*buttons++ = (sign((*i)->control_ref->State()) * range);
				else
					*buttons++ = 0.f;
			}
		}

	private:
		bool	m_has_range;
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
				{
					*analog = S(controls[i+trig_count]->control_ref->State() * range);
				}
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

			for (unsigned int i = 0; i < 3; i++)
			{
				float dz = 0;
				const float state = controls[i*2 + 1]->control_ref->State() - controls[i*2]->control_ref->State();

				if (fabsf(state) > deadzone)
					dz = ((state - (deadzone * sign(state))) / (1 - deadzone));

				if (step)
				{
					if (state > m_swing[i])
						m_swing[i] = std::min(m_swing[i] + 0.1f, state);
					else if (state < m_swing[i])
						m_swing[i] = std::max(m_swing[i] - 0.1f, state);
				}

				// deceleration switch
				m_state[i] = (abs(m_swing[i]) >= 0.7
					? -2 * sign(state) + m_swing[i] * 2
					: m_swing[i]) * sign(state);

				*axis++ = (C)(m_state[i] * range * master_range + base);
			}
		}
	private:
		float	m_swing[3], m_state[3];
	};

	class Tilt : public ControlGroup
	{
	public:
		Tilt(const char* const _name, bool gyro = false);

		bool HasGyro() { return m_has_gyro; }

		template <typename C, typename R>
		void GetState(C* const pitch, C* const roll, C* const yaw, const bool gyro = false, const unsigned int base = 0.0, const R range = 1.0, const bool step = true)
		{
			ControlState state;
			ControlState r_acc = controls[m_has_gyro ? T_ACC_RANGE : T_RANGE]->control_ref->State();
			auto const r_acc_s = settings[m_has_gyro ? T_ACC_RANGE_S : T_RANGE]->value;
			auto const deadzone = settings[m_has_gyro ? T_DEADZONE : T_DEADZONE_N]->value;
			auto const circle = settings[m_has_gyro ? T_CIRCLESTICK : T_CIRCLESTICK_N]->value;
			auto const angle = settings[m_has_gyro ? T_ANGLE : T_ANGLE_N]->value / 1.8f;

			ControlState settle = 0
				, r_gyro_1 = 1.0
				, r_gyro_2 = 1.0
				, r_gyro_s = 1.0;
			if (m_has_gyro)
			{
				r_gyro_1 = controls[T_GYRO_RANGE_1]->control_ref->State();
				r_gyro_2 = controls[T_GYRO_RANGE_2]->control_ref->State();
				r_gyro_s = settings[T_GYRO_RANGE]->value;
				settle = settings[T_GYRO_SETTLE]->value;
			}

			for (unsigned int i = 0; i < 3; i++)
			{
				state = controls[i*2 + 1]->control_ref->State() - controls[i*2]->control_ref->State();

				// deadzone
				state *= fabsf(state)>deadzone
					? (fabsf(state)-deadzone)/(1.0-deadzone)
					: 0.0;

				if (step)
				{
					if (state == m_acc[i])
						m_settle[i]++;
					else
						m_settle[i] = 0;

					// step towards state
					if (state > m_acc[i])
						m_acc[i] = std::min(m_acc[i] + 0.1f, state);
					else if (state < m_acc[i])
						m_acc[i] = std::max(m_acc[i] - 0.1f, state);

					// step gyro towards 0
					if (m_settle[i] > settle * 100.0)
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
				// I think this is more like a pointed circle rather than a rounded square like it should be
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

			if (gyro)
			{
				if (!r_gyro_1) r_gyro_1 = 1.0;
				if (!r_gyro_2) r_gyro_2 = 1.0;
				*pitch = C(m_gyro[0] * angle * range * r_gyro_1 * r_gyro_2 * r_gyro_s + base);
				*roll = C(m_gyro[1] * angle * range * r_gyro_1 * r_gyro_2 * r_gyro_s + base);
				*yaw = C(m_gyro[2] * angle * range * r_gyro_1 * r_gyro_2 * r_gyro_s + base);
			}
			else
			{
				if (!r_acc) r_acc = 1.0;
				*pitch = C(m_acc[0] * angle * range * r_acc * r_acc_s + base);
				*roll = C(m_acc[1] * angle * range * r_acc * r_acc_s + base);
				*yaw = C(m_acc[2] * angle * range * r_acc * r_acc_s + base);
			}
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
			std::lock_guard<std::mutex> lk(m_mutex);

			// smooth frames
			const u8 NUM_FRAMES = 10;
			// absolute conversion range
			const float ABS_RANGE = .2,
			// raw input range
			RAW_RANGE = .1;

			ControlState r = controls[C_RANGE_MODIFIER]->control_ref->State();
			if (!r)
				r = 1.0;

			for (unsigned int i=0; i<3; i++)
			{
				bool is_relative = controls[i*2 + 1]->control_ref->IsRelative() || controls[i*2]->control_ref->IsRelative();
				ControlState state = controls[i*2 + 1]->control_ref->State() - controls[i*2]->control_ref->State();

				// change sign
				if (i == 0)
					state = -state;

				// update absolute position
				if (is_relative)
				{
					// moving average smooth input
					m_list[i].push_back(state);
					float fsum = 0; int j = 0;
					if (m_list[i].size() > 0)
					{
						for (std::list<float>::reverse_iterator it = m_list[i].rbegin(); relative ? it != m_list[i].rend() : j < 1; it++, j++)
							fsum += *it;
						state = fsum / float(m_list[i].size());
					}
					if (m_list[i].size() >= NUM_FRAMES)
						m_list[i].pop_front();

					// update position
					m_absolute[i] += state * range * r * ABS_RANGE;

					m_absolute[i] = MathUtil::Trim(m_absolute[i], -1, 1);
					m_state[i] = m_absolute[i];
				}
				else
				{
					m_absolute[i] = MathUtil::Trim(state, -1, 1);
				}

				if (relative)
				{
					m_state[i] = (m_absolute[i] - m_last[i]) * RAW_RANGE;
					if (step)
						m_last[i] = m_absolute[i];
				}
				else
				{
					m_state[i] = m_absolute[i];
				}
			}

			*y = m_state[0];
			*x = m_state[1];
			*z = m_state[2];

			// adjust absolute cursor
			if (adjusted && !relative)
			{
				*x *= (settings[C_WIDTH]->value * range * 2.0);
				*y *= (settings[C_HEIGHT]->value * range * 2.0);
				*y += (settings[C_CENTER]->value - 0.5f);
			}
		}

		float				m_state[3], m_absolute[3], m_last[3];
		std::list<float>	m_list[3];
		std::mutex			m_mutex;
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
