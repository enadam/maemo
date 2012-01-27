#!/usr/bin/python
#
# pls.py -- acquire permission for VideoPlayback and execute a command
#
# Synopsis:
#   pls.py <command> [<args>]...
#
# Ask VideoPlayback permission from the resource manager through D-BUS,
# wait for <command>'s completition, then release the resource.
#

# We'll need to tell the PID of the process to be privileged.
import os
(r, w) = os.pipe()
pid = os.fork()
if pid == 0:
	import sys

	# Wait until we get green lights.
	os.close(w)
	os.read(r, 1)
	os.close(r)

	# Execute <command>.
	del sys.argv[0]
	if len(sys.argv) > 0:
		os.execvp(sys.argv[0], sys.argv)
else:
	os.close(r)

	# Ignore ^C.
	import signal
	signal.signal(signal.SIGINT, signal.SIG_IGN)

	# Send the request to the resource manager and wait for the replies
	# (registration, acquision, configuration).
	import dbus
	dst = [
		"org.maemo.resource.manager",
		"/org/maemo/resource/manager",
		"org.maemo.resource.manager",
	]

	# Constants from libresource
	RESMSG_REGISTER			= 0
	RESMSG_UNREGISTER		= 1
	RESMSG_UPDATE			= 2
	RESMSG_ACQUIRE			= 3
	RESMSG_RELEASE			= 4
	RESMSG_GRANT			= 5
	RESMSG_ADVICE			= 6
	RESMSG_AUDIO			= 7
	RESMSG_VIDEO			= 8

	RESOURCE_AUDIO_PLAYBACK		= 1 <<  0
	RESOURCE_VIDEO_PLAYBACK		= 1 <<  1
	RESOURCE_AUDIO_RECORDING	= 1 <<  2
	RESOURCE_VIDEO_RECORDING	= 1 <<  3
	RESOURCE_VIBRA			= 1 <<  4
	RESOURCE_LEDS			= 1 <<  5
	RESOURCE_BACKLIGHT		= 1 <<  6
	RESOURCE_SYSTEM_BUTTON		= 1 <<  8
	RESOURCE_LOCK_BUTTON		= 1 <<  9
	RESOURCE_SCALE_BUTTON		= 1 << 10
	RESOURCE_SNAP_BUTTON		= 1 << 11
	RESOURCE_LENS_COVER		= 1 << 12
	RESOURCE_HEADSET_BUTTONS	= 1 << 13
	RESOURCE_LARGE_SCREEN		= 1 << 14

	caps = RESOURCE_VIDEO_PLAYBACK
	msgs = []
	msgs.append(dbus.lowlevel.MethodCallMessage(*dst + ["register"]))
	msgs[-1].append(
		dbus.types.Int32(RESMSG_REGISTER),	# type
		dbus.types.UInt32(0),			# id
		dbus.types.UInt32(len(msgs)+1),		# reqno
		dbus.types.UInt32(caps),		# rset.all
		dbus.types.UInt32(0),			# rset.opt
		dbus.types.UInt32(0),			# rset.share
		dbus.types.UInt32(0),			# rset.mask
		dbus.types.String("player"),		# class
		dbus.types.UInt32(0))			# mode
	msgs.append(dbus.lowlevel.MethodCallMessage(*dst + ["acquire"]))
	msgs[-1].append(
		dbus.types.Int32(RESMSG_ACQUIRE),	# type
		dbus.types.UInt32(0),			# id
		dbus.types.UInt32(len(msgs)+1))		# reqno
	msgs.append(dbus.lowlevel.MethodCallMessage(*dst + ["video"]))
	msgs[-1].append(
		dbus.types.Int32(RESMSG_VIDEO),		# type
		dbus.types.UInt32(0),			# id
		dbus.types.UInt32(len(msgs)+1),		# reqno
		dbus.types.UInt32(pid))			# pid
	msgs.append(dbus.lowlevel.MethodCallMessage(*dst + ["release"]))
	msgs[-1].append(
		dbus.types.Int32(RESMSG_RELEASE),	# type
		dbus.types.UInt32(0),			# id
		dbus.types.UInt32(len(msgs)+1))		# reqno

	# Send the register, acquire and video messages.
	for msg in msgs[0:-1]:
		dbus.SystemBus().send_message_with_reply_and_block(msg)
	print "acquired"

	# Tell the child that the permission has arrived.
	os.write(w, "go")
	os.wait()

	# Unregister.
	dbus.SystemBus().send_message(msgs[-1])
	print "released"

# End of pls.py
