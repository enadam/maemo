#!/usr/bin/python
#
# vkb.py -- open the virtual keyboard
#
# Synposis:
#   vkb.py [<window-id>]
#
# <window-id> is the XID of the window to which the VKB is attached.
# If omitted, the topmost application window is assumed.
#
# The program runs until end of input is encountered (^D), when the
# VKB is closed (as long as we're in control).  By hitting Enter you
# can hide it temporarily and show it again.
#
# The VKB we open does not follow the orientation of the application
# automatically.  You can synchronize them by inputting 'r', or you
# can rotate freely clockwise, counter-clockwise or by 180 degrees
# with "r+", "r-" and "r!".  Using capital 'R' will skip the animation.
#

import sys
import dbus
import dbus.types as t

# Get the topmost window ID, which the uiserver needs to know.
if len(sys.argv) <= 1:
	import os

	xprop = "xprop -root -notype _NET_ACTIVE_WINDOW"
	win = os.popen(xprop).readline().split()[-1]
else:
	win = sys.argv[1]

# Convert from string to integer and handle hexa numbers.
win = int(win, 0)

# Construct the messages for meego-im-uiserver.
dst = (None,
	"/com/meego/inputmethod/uiserver1",
	"com.meego.inputmethod.uiserver1")
activate = dbus.lowlevel.MethodCallMessage(*dst + ("activateContext",))
config   = dbus.lowlevel.MethodCallMessage(*dst + ("updateWidgetInformation",))
config.append(t.Dictionary({
	t.String("focusState"):		t.Boolean(True, variant_level=1),
	t.String("toolbarId"):		t.Int32(-1, variant_level=1),
	t.String("contentType"):	t.Int32(0, variant_level=1),
	t.String("correctionEnabled"):	t.Boolean(False, variant_level=1),
	t.String("predictionEnabled"):	t.Boolean(False, variant_level=1),
	t.String("autocapitalizationEnabled"): t.Boolean(False,
						variant_level=1),
	t.String("hiddenText"):		t.Boolean(False, variant_level=1),
	t.String("inputMethodMode"):	t.Int32(0, variant_level=1),
	t.String("hasSelection"):	t.Boolean(False, variant_level=1),
	t.String("winId"):		t.UInt32(win, variant_level=1)}))
config.append(t.Boolean(True))		# focusChanged
show = dbus.lowlevel.MethodCallMessage(*dst + ("showInputMethod",))
hide = dbus.lowlevel.MethodCallMessage(*dst + ("hideInputMethod",))
#jo vagy nalam szivi

# Send the messages.
conn = dbus.connection.Connection("unix:path=/tmp/meego-im-uiserver/imserver_dbus")
for msg in (activate, config, show):
	msg.set_no_reply(True)
	conn.send_message(msg)
conn.flush()

# When we exit, the vkb closes.
rot = None
shown = True
while True:
	sys.stdout.write('')
	if shown:
		print "hide",
	else:
		print "show",
	line = sys.stdin.readline()
	if line == '':
		break

	# Rotate?
	if line[0] == 'r' or line[0] == 'R':
		# Get the rotation of $win.
		def get_rot(win):
			import os

			xprop = "xprop -id %u -notype %s " % \
				(win, '_MEEGOTOUCH_ORIENTATION_ANGLE')
			now = os.popen(xprop).readline().split()[-1]
			if not now.endswith('not found.\n'):
				return int(now)
			else:
				return 0

		# Tell the uiserver to rotate.
		def send_rot(fun, rot):
			msg = dbus.lowlevel.MethodCallMessage(
				*dst + (fun,))
			msg.append(t.Int32(rot))
			conn.send_message(msg)
			conn.flush()

		# Where to rotate?
		if line[1] == '+' or line[1] == '-' or line[1] == '!':
			if rot is None:
				import os

				# Get the current rotation of the VKB.
				# We need to learn the VKB window first.
				mim = win
				xwininfo = 'xwininfo -name MInputMethod'
				for l in os.popen(xwininfo):
					if l.startswith('xwininfo: Window id:'):
						mim = int(l.split()[3], 0)
				rot = get_rot(mim)

			# Calculate the next angle.
			if line[1] == '+':
				# Rotate clockwise.
				rot = (rot + 90)  % 360
			elif line[1] == '-':
				# Rotate counter-clockwise.
				rot = (rot + 270) % 360
			elif line[1] == '!':
				# Turn around.
				rot = (rot + 180) % 360
		else:
			# Synchronize the VKB with the application window.
			rot = get_rot(win)

		if line[0] == 'r':
			# Animate.
			send_rot('appOrientationAboutToChange', rot)
		send_rot('appOrientationChanged', rot)
		continue

	# Show or hide VKB.
	if shown:
		conn.send_message(hide)
	else:
		conn.send_message(activate)
		conn.send_message(show)
	conn.flush()
	shown = not shown

# End of vkb.py
