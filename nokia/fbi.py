#!/usr/bin/python
#
# fbi.py -- ask WANO to let you out
#
# This script logs in or out of WANO for you, freeing you from accessing
# the guest.nokia.com web interface manually.  It has both command line
# and graphical user interfaces.
#
# Synopsis:
#   fbi.py [{x|logout}]
#
# x:		use the GUI anyway
# logout:	if using the command line interface, log out of WANO
#
# In order to use the graphical user interface $DISPLAY must be defined
# and either the standard input should not be a TTY or you must specify
# 'x' on the command line.  In any other cases the command line interface
# is used.
#

# Communicate with guest.nokia.com.  @islogin is either '1' (login)
# or '2' (logout), following the service codes.  Returns the textual
# description of the result of the request.
def work(username, password, islogin):
	import re, urllib

	# We need to weed through multiple pages.
	gnc = "https://guest.nokia.com:950"

	# Find the ID that must be present in all requests.
	r = re.compile('^<INPUT TYPE="hidden" NAME="ID" VALUE="(\w+)">')
	for line in urllib.urlopen(gnc):
		m = r.match(line)
		if m != None:
			id = m.group(1)
			break
	assert(id != None)

	# 'STATE' tells the service what we request.
	# The order of the fields is significant.
	urllib.urlopen(gnc, urllib.urlencode((
		('ID',		id),
		('STATE',	'1'),
		('accesstype',	'Visitor'),
		('DATA',	username + '@visitor.nokia.com')))).close()
	urllib.urlopen(gnc, urllib.urlencode((
		('ID',		id),
		('STATE',	'2'),
		('DATA',	password)))).close()
	reply = urllib.urlopen(gnc, urllib.urlencode((
		('ID',		id),
		('STATE',	'3'),
		('DATA',	islogin))))

	# Find the result message.
	r = re.compile('^Authentication message: ([\w ]+)')
	for line in reply:
		m = r.match(line)
		if m != None:
			return m.group(1)
	return 'Vittu, jee!'

# Put up a GUI banner.
def say(what, size=35):
	import gtk, pango

	banner = gtk.Window()
	label = gtk.Label(what)
	if size != None:
		label.modify_font(pango.FontDescription(str(size)));
	banner.add(label)

	# Tell the window manager @banner is a banner.
	banner.realize()
	banner.window.property_change('_NET_WM_WINDOW_TYPE',
		'ATOM', 32, gtk.gdk.PROP_MODE_REPLACE,
		[gtk.gdk.atom_intern('_NET_WM_WINDOW_TYPE_NOTIFICATION')])
	banner.window.property_change('_HILDON_NOTIFICATION_TYPE',
		'STRING', 8, gtk.gdk.PROP_MODE_REPLACE,
		'_HILDON_NOTIFICATION_TYPE_BANNER')
	
	# Wait until the user confirms by clicking on it.
	banner.add_events(gtk.gdk.BUTTON_PRESS_MASK)
	banner.connect("button-press-event", lambda a, b: gtk.main_quit())
	banner.show_all()
	gtk.main()
	banner.destroy()

# Put up a large banner.
def yell(what):
	say(what, 200)

# The GUI interface.
def gui():
	import time
	import gobject, gtk, pygtk

	# Create a dialog to ask for the user name and password.
	dlg = gtk.Dialog()
	dlg.set_title('F* BI!')
	dlg.add_button("Log in",  1)
	dlg.add_button("Log out", 2)
	
	vbox = gtk.VBox()
	dlg.get_child().get_child().add(vbox)

	hbox = gtk.HBox()
	hbox.add(gtk.Label('NOE user:'))
	username = gtk.Entry()
	username.set_visibility(False)
	username.set_invisible_char(unichr(0x263A))
	hbox.add(username)
	vbox.add(hbox)

	hbox = gtk.HBox()
	hbox.add(gtk.Label('Password:'))
	password = gtk.Entry()
	password.set_visibility(False)
	password.set_invisible_char(unichr(0x2639))
	hbox.add(password)
	vbox.add(hbox)

	# Keep asking until we get a sane answer, then work().
	dlg.show_all()
	while True:
		ret = dlg.run()
		if ret < 0:
			return
		usr = username.get_text()
		pwd = password.get_text()
		if usr != '' and pwd != '':
			break
		yell('Hey!')
		time.sleep(0.1)
	say(work(usr, pwd, ret))

# The command line interface.
def cmdline():
	import getpass

	# Ask the user name and password safely then work().
	username = getpass.getpass('No, ki a faszagyerek: ')
	password = getpass.getpass(username + ', te tudod: ')
	if len(sys.argv) < 2 or sys.argv[1] != 'logout':
		islogin = '1'
	else:
		islogin = '2'
	print work(username, password, islogin)

# Main starts here
import os, sys

# Choose the user interface.
if not os.environ.has_key('DISPLAY'):
	cmdline()
elif len(sys.argv) >= 2 and sys.argv[1] == 'x':
	gui()
elif os.isatty(0):
	cmdline()
else:
	gui()

# End of fbi.py
