/** @file axisinputcontrol.cpp  Axis control for a logical input device.
 *
 * @authors Copyright © 2003-2017 Jaakko Keränen <jaakko.keranen@iki.fi>
 * @authors Copyright © 2005-2014 Daniel Swanson <danij@dengine.net>
 *
 * @par License
 * GPL: http://www.gnu.org/licenses/gpl.html
 *
 * <small>This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by the
 * Free Software Foundation; either version 2 of the License, or (at your
 * option) any later version. This program is distributed in the hope that it
 * will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty
 * of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU General
 * Public License for more details. You should have received a copy of the GNU
 * General Public License along with this program; if not, write to the Free
 * Software Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA
 * 02110-1301 USA</small>
 */

#include "ui/axisinputcontrol.h"
#include <de/smoother.h>
#include <de/timer.h> // SECONDSPERTIC
#include <de/Block>
#include <doomsday/console/var.h>
#include "ui/joystick.h"
#include "dd_loop.h" // DD_LatestRunTicsStartTime()

using namespace de;

static dfloat const AXIS_NORMALIZE = 1.f / float(IJOY_AXISMAX); // Normalize from SDL's range

DENG2_PIMPL_NOREF(AxisInputControl)
{
    Type type = Pointer;
    dint flags = 0;

    ddouble position       = 0; ///< Current translated position (-1..1) including any filtering.
    ddouble realPosition   = 0; ///< The actual latest position (-1..1).
    ddouble markedPosition = 0;

    dfloat offset   = 0;           ///< Offset to add to real input value.
    dfloat scale    = 1;           ///< Scaling factor for real input values.
    dfloat deadZone = 0;           ///< Dead zone in (0..1) range.

    ddouble sharpPosition = 0;     ///< Current sharp (accumulated) position, entered into the Smoother.
    Smoother *smoother = nullptr;  ///< Smoother for the input values (owned).
    ddouble prevSmoothPos = 0;     ///< Previous evaluated smooth position (needed for producing deltas).

    duint time = 0;                ///< Timestamp of the last position update.

    Impl()
    {
        Smoother_SetMaximumPastNowDelta(smoother = Smoother_New(), 2 * SECONDSPERTIC);
    }

    ~Impl()
    {
        Smoother_Delete(smoother);
    }

#if 0
    static float filter(int grade, float *accumulation, float ticLength)
    {
        DENG2_ASSERT(accumulation);
        int dir     = de::sign(*accumulation);
        float avail = fabs(*accumulation);
        // Determine the target velocity.
        float target = avail * (MAX_AXIS_FILTER - de::clamp(1, grade, 39));

        /*
        // test: clamp
        if (target < -.7) target = -.7;
        else if (target > .7) target = .7;
        else target = 0;
        */

        // Determine the amount of mickeys to send. It depends on the
        // current mouse velocity, and how much time has passed.
        float used = target * ticLength;

        // Don't go past the available motion.
        if (used > avail)
        {
            *accumulation = nullptr;
            used = avail;
        }
        else
        {
            if (*accumulation > nullptr)
                *accumulation -= used;
            else
                *accumulation += used;
        }

        // This is the new (filtered) axis position.
        return dir * used;
    }
#endif
};

AxisInputControl::AxisInputControl(String const &name, Type type) : d(new Impl)
{
    setName(name);
    d->type = type;
}

AxisInputControl::~AxisInputControl()
{}

AxisInputControl::Type AxisInputControl::type() const
{
    DENG2_GUARD(this);
    return d->type;
}

void AxisInputControl::setRawInput(bool yes)
{
    DENG2_GUARD(this);
    if (yes) d->flags |= IDA_RAW;
    else     d->flags &= ~IDA_RAW;
}

bool AxisInputControl::isActive() const
{
    DENG2_GUARD(this);
    return (d->flags & IDA_DISABLED) == 0;
}

bool AxisInputControl::isInverted() const
{
    DENG2_GUARD(this);
    return (d->flags & IDA_INVERT) != 0;
}

void AxisInputControl::update(timespan_t ticLength)
{
    DENG2_GUARD(this);

    Smoother_Advance(d->smoother, ticLength);

    if (d->type == Stick)
    {
        if (d->flags & IDA_RAW)
        {
            // The axis is supposed to be unfiltered.
            d->position = d->realPosition;
        }
        else
        {
            // Absolute positions are straightforward to evaluate.
            Smoother_EvaluateComponent(d->smoother, 0, &d->position);
        }
    }
    else if (d->type == Pointer)
    {
        if (d->flags & IDA_RAW)
        {
            // The axis is supposed to be unfiltered.
            d->position    += d->realPosition;
            d->realPosition = 0;
        }
        else
        {
            // Apply smoothing by converting back into a delta.
            coord_t smoothPos = d->prevSmoothPos;
            Smoother_EvaluateComponent(d->smoother, 0, &smoothPos);
            d->position     += smoothPos - d->prevSmoothPos;
            d->prevSmoothPos = smoothPos;
        }
    }

    // We can clear the expiration now that an updated value is available.
    setBindContextAssociation(Expired, UnsetFlags);
}

ddouble AxisInputControl::position() const
{
    DENG2_GUARD(this);
    return d->position;
}

void AxisInputControl::setPosition(ddouble newPosition)
{
    DENG2_GUARD(this);
    d->position = newPosition;
}

void AxisInputControl::markPosition()
{
    d->markedPosition = d->position;
}

ddouble AxisInputControl::markedPosition() const
{
    return d->markedPosition;
}

void AxisInputControl::applyRealPosition(dfloat pos)
{
    DENG2_GUARD(this);

    dfloat const oldRealPos  = d->realPosition;
    dfloat const transformed = translateRealPosition(pos);

    // The unfiltered position.
    d->realPosition = transformed;

    if (oldRealPos != d->realPosition)
    {
        // Mark down the time of the change.
        d->time = DD_LatestRunTicsStartTime();
    }

    if (d->type == Stick)
    {
        d->sharpPosition = d->realPosition;
    }
    else // Cumulative.
    {
        // Convert the delta to an absolute position for smoothing.
        d->sharpPosition += d->realPosition;
    }

    Smoother_AddPosXY(d->smoother, DD_LatestRunTicsStartTime(), d->sharpPosition, 0);
}

dfloat AxisInputControl::translateRealPosition(dfloat realPos) const
{
    DENG2_GUARD(this);

    // An inactive axis is always zero.
    if (!isActive()) return 0;

    // Apply scaling, deadzone and clamping.
    float outPos = realPos * AXIS_NORMALIZE * d->scale;
    if (d->type == Stick) // Only stick axes are dead-zoned and clamped.
    {
        outPos += d->offset;

        if (std::abs(outPos) <= d->deadZone)
        {
            outPos = 0;
        }
        else
        {
            outPos -= d->deadZone * de::sign(outPos);  // Remove the dead zone.
            outPos *= 1.0f/(1.0f - d->deadZone);       // Normalize.
            outPos = de::clamp(-1.0f, outPos, 1.0f);
        }
    }

    if (isInverted())
    {
        // Invert the axis position.
        outPos = -outPos;
    }

    return outPos;
}

dfloat AxisInputControl::deadZone() const
{
    DENG2_GUARD(this);
    return d->deadZone;
}

void AxisInputControl::setDeadZone(dfloat newDeadZone)
{
    DENG2_GUARD(this);
    d->deadZone = newDeadZone;
}

dfloat AxisInputControl::scale() const
{
    DENG2_GUARD(this);
    return d->scale;
}

void AxisInputControl::setScale(dfloat newScale)
{
    DENG2_GUARD(this);
    d->scale = newScale;
}

dfloat AxisInputControl::offset() const
{
    DENG2_GUARD(this);
    return d->offset;
}

void AxisInputControl::setOffset(dfloat newOffset)
{
    DENG2_GUARD(this);
    d->offset = newOffset;
}

duint AxisInputControl::time() const
{
    DENG2_GUARD(this);
    return d->time;
}

String AxisInputControl::description() const
{
    DENG2_GUARD(this);

    QStringList flags;
    if (!isActive()) flags << "disabled";
    if (isInverted()) flags << "inverted";

    String flagsString;
    if (!flags.isEmpty())
    {
        String flagsAsText = flags.join("|");
        flagsString = String(_E(l) " Flags :" _E(.)_E(i) "%1" _E(.)).arg(flagsAsText);
    }

    return String(_E(b) "%1 " _E(.) "(%2)"
                  _E(l) " Current value: " _E(.) "%3"
                  _E(l) " Deadzone: " _E(.) "%4"
                  _E(l) " Scale: "     _E(.) "%5"
                  _E(l) " Offset: "     _E(.) "%6"
                  "%7")
               .arg(fullName())
               .arg(d->type == Stick? "Stick" : "Pointer")
               .arg(position())
               .arg(d->deadZone)
               .arg(d->scale)
               .arg(d->offset)
               .arg(flagsString);
}

bool AxisInputControl::inDefaultState() const
{
    DENG2_GUARD(this);
    return d->position == 0; // Centered?
}

void AxisInputControl::reset()
{
    DENG2_GUARD(this);
    if (d->type == Pointer)
    {
        // Clear the accumulation.
        d->position      = 0;
        d->sharpPosition = 0;
        d->prevSmoothPos = 0;
    }
    Smoother_Clear(d->smoother);
}

void AxisInputControl::consoleRegister()
{
    DENG2_GUARD(this);

    DENG2_ASSERT(hasDevice() && !name().isEmpty());
    String controlName = String("input-%1-%2").arg(device().name()).arg(name());

    Block scale = (controlName + "-factor").toUtf8();
    C_VAR_FLOAT(scale.constData(), &d->scale, CVF_NO_MAX, 0, 0);

    Block flags = (controlName + "-flags").toUtf8();
    C_VAR_INT(flags.constData(), &d->flags, 0, 0, 7);

    if (d->type == Stick)
    {
        Block deadzone = (controlName + "-deadzone").toUtf8();
        C_VAR_FLOAT(deadzone.constData(), &d->deadZone, 0, 0, 1);

        Block offset = (controlName + "-offset").toUtf8();
        C_VAR_FLOAT(offset.constData(), &d->offset, CVF_NO_MAX | CVF_NO_MIN, 0, 0);
    }
}
