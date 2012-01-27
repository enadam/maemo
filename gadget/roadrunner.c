#if 0 // compile me like this:
  pkgs="pkg-config --cflags --libs hildon-1 libhildonmime dbus-glib-1";
  cc         -Wall                 `$pkgs`           "$0" "$@";
  cc -shared -Wall -DHAVE_SYSTEMUI `$pkgs gconf-2.0` "$0" "$@.so";
  exit;
#endif
/*
 * roadrunner.c -- simple always accessible Run dialog
 *
 * Watches the camera key, and if you half-press it long enough opens a
 * file chooser dialog, where you can select what to launch wnywhere in
 * the file system.  Roadrunner can be started standalone, or can be
 * installed as a systemui plugin in /usr/lib/systemui, starting it
 * automatically at boot.
 */

/* Include files */
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <string.h>
#include <ctype.h>

#include <hildon/hildon.h>
#include <hildon-mime.h>

#include <dbus/dbus.h>
#include <dbus/dbus-glib-lowlevel.h>

#ifdef HAVE_SYSTEMUI
# include <systemui/appdata.h>
#endif

/* Private variables */
static unsigned Timer = 0;
static GHashTable *Children;
static GtkWidget *Dialog;

/* Program code */
/* The camera key has been pressed long enough, open the dialog. */
static gboolean timeout(GtkDialog *dialog)
{
	Timer = 0;
	gtk_dialog_run(dialog);
	return FALSE;
} /* timeout */

/* Watch HAL for changes in the state of the camera key. */
static DBusHandlerResult dbus_filter(DBusConnection *con, DBusMessage *msg,
	GtkDialog *dialog)
{
	if (dbus_message_is_signal(msg, "org.freedesktop.Hal.Device", "Condition"))
	{
		static gboolean pressed = FALSE;

		/* Unfortunately HAL doesn't tell in the notification
		 * messages what the new state is. */
		pressed = !pressed;
		if (pressed && !GTK_WIDGET_VISIBLE(dialog))
			/* Open the dialog in a while. */
			Timer = g_timeout_add_seconds(
				3, (GSourceFunc)timeout, dialog);
		else if (!pressed && Timer)
		{
			g_source_remove(Timer);
			Timer = 0;
		}
	}

	return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;
} /* dbus_filter */

/* A child is dead, remove it from $Children. */
static void death(GPid pid, gint status, char *fname)
{
	GList *pids, *newpids;

	g_debug("DIED %d", pid);
	if (!(pids = g_hash_table_lookup(Children, fname)))
		/* NOP */;
	else if (!(newpids = g_list_remove(pids, GINT_TO_POINTER(pid))))
	{
		char *current;

		g_hash_table_remove(Children, fname);
		g_debug("DONE WITH %s", fname);

		/* Deactivate the button if the last child of this
		 * program is dead. */
		current = gtk_file_chooser_get_filename(
			GTK_FILE_CHOOSER(Dialog));
		if (!strcmp(fname, current))
		{
			if (GTK_WIDGET_VISIBLE(Dialog))
				hildon_banner_show_information(NULL, NULL,
					"Stopped");
			gtk_widget_set_sensitive(
				gtk_file_chooser_get_extra_widget(
					GTK_FILE_CHOOSER(Dialog)), FALSE);
		}

		g_free(current);
		g_free(fname);
	} else if (pids != newpids)
	{
		g_hash_table_insert(Children, fname, newpids);
		g_debug("UPDATED %s", fname);
	}
} /* death */

/* Launch a child, store its PID and start watching it. */
static gboolean spawn(char *fname, char **argv)
{
	GPid pid;
	GList *pids;
	GError *err;

	err = NULL;
	g_debug("SPAWN %s", fname);
	g_spawn_async(getenv("HOME"), argv, NULL,
		G_SPAWN_DO_NOT_REAP_CHILD, NULL, NULL, &pid, &err);
        if (err)
	{
		g_debug("FAIL %s", err->message);
		hildon_banner_show_information(NULL, NULL, err->message);
		g_error_free(err);
		return FALSE;
	} else
		g_child_watch_add(pid, (GChildWatchFunc)death, g_strdup(fname));

	pids = g_hash_table_lookup(Children, fname);
	g_hash_table_replace(Children, fname,
		g_list_prepend(pids, GINT_TO_POINTER(pid)));

	hildon_banner_show_information(NULL, NULL, "Executed");
	return TRUE;
} /* spawn */

/* Returns $fname looks like plain text. */
static gboolean is_text(char const *fname)
{
	gboolean isit;
	char buf[128];
	int fd, lbuf, i;

	if ((fd = open(fname, O_RDONLY)) < 0)
		return FALSE;
	lbuf = read(fd, buf, sizeof(buf));
	close(fd);

	isit = TRUE;
	for (i = 0; i < lbuf && isit; i++)
		isit = isascii(buf[i]);
	return isit;
} /* is_text */

/* Try to open $fname with an application that handles its MIME type,
 * or show it with an editor if it's a text file. */
static gboolean mime_open(DBusConnection *dbus, char const *fname)
{
	if (hildon_mime_open_file(dbus, fname) == 1)
		return TRUE;
	if (!is_text(fname))
		return FALSE;
	if (hildon_mime_open_file_with_mime_type(dbus, fname, "text/plain") != 1)
		return FALSE;
	return TRUE;
} /* mime_open */

/* A file was double-clicked in the dialog, execute it. */
static void activate(GtkFileChooser *dlg, GtkButton *button)
{
	char *fname;
	gboolean isok;

	isok = FALSE;
	fname = gtk_file_chooser_get_filename(dlg);
	if (access(fname, X_OK) == 0)
	{	/* Directly executable */
		char *argv[2];

		argv[0] = fname;
		argv[1] = NULL;
		isok = spawn(fname, argv);
	} else if (g_str_has_suffix(fname, ".sh"))
	{	/* Shell script, run with /bin/sh. */
		char *argv[3];

		argv[0] = "/bin/sh";
		argv[1] = fname;
		argv[2] = NULL;
		isok = spawn(fname, argv);
	} else
	{
		DBusConnection *dbus;

		dbus = dbus_bus_get(DBUS_BUS_SESSION, NULL);
		if (!mime_open(dbus, fname))
			hildon_banner_show_information(NULL, NULL,
				"Unable to open with anything");
		dbus_connection_unref(dbus);
	}

	if (isok)
		gtk_widget_set_sensitive(GTK_WIDGET(button), TRUE);
} /* activate */

/* Stop the children spawned by the selected program. */
static void stop(GtkButton *button, GtkFileChooser *dlg)
{
	char *fname;
	GList *pids;

	fname = gtk_file_chooser_get_filename(dlg);
	g_debug("STOP %s", fname);
	for (pids = g_hash_table_lookup(Children, fname); pids;
		pids = pids->next)
	{
		g_debug("KILL %d", GPOINTER_TO_INT(pids->data));
		kill(GPOINTER_TO_INT(pids->data), SIGTERM);
	}
	g_free(fname);
} /* stop */

static void selected(GtkFileChooser *dlg, GtkButton *button)
{
	char *fname;

	if (!(fname = gtk_file_chooser_get_filename(dlg)))
		return;
	gtk_widget_set_sensitive(GTK_WIDGET(button), 
		g_hash_table_lookup(Children, fname) != NULL);
	g_free(fname);
}

/* Create the file chooser dialog and setup the watch for the camera key
 * (actually the cam focus key, which is the half-pressed camera key). */
static GtkDialog *init(DBusConnection *dbus)
{
	GtkWidget *dlg, *button;

	Children = g_hash_table_new_full(g_str_hash, g_str_equal,
		g_free, NULL);

	Dialog = dlg = gtk_file_chooser_dialog_new("Roadrunner", NULL,
		GTK_FILE_CHOOSER_ACTION_OPEN,
		GTK_STOCK_CANCEL, GTK_RESPONSE_CANCEL,
		NULL, GTK_RESPONSE_ACCEPT, NULL);
	gtk_file_chooser_set_current_folder(GTK_FILE_CHOOSER(dlg), "/home/user");
	g_object_set(G_OBJECT(dlg), "show-hidden", TRUE, NULL);
	g_signal_connect(dlg, "delete-event", dbus != NULL
		? G_CALLBACK(gtk_widget_hide_on_delete) : G_CALLBACK(exit),
		NULL);

	button = gtk_button_new_with_label("Stop");
	g_signal_connect(button, "clicked", G_CALLBACK(stop), dlg);
	gtk_widget_show(button);
	gtk_file_chooser_set_extra_widget(GTK_FILE_CHOOSER(dlg), button);

	g_signal_connect(dlg, "file-activated",
		G_CALLBACK(activate), button);
	g_signal_connect(dlg, "selection-changed",
		G_CALLBACK(selected), button);

	if (dbus != NULL)
	{	/* No need for this in oneshot mode. */
		dbus_bus_add_match(dbus,
			"type='signal',"
			"path='/org/freedesktop/Hal/devices/platform_cam_focus',"
			"interface='org.freedesktop.Hal.Device',"
			"member='Condition'", NULL);
		dbus_connection_add_filter(dbus,
			(DBusHandleMessageFunction)dbus_filter, dlg, NULL);
	}

	return GTK_DIALOG(dlg);
} /* init */

#ifdef HAVE_SYSTEMUI
gboolean plugin_init(AppUIData *sysui)
{
	init(sysui->conn);
	return TRUE;
}

void plugin_close(void *data)
{
	/* NOP */
}
#else /* ! HAVE_SYSTEMUI */
int main(int argc, char const *argv[])
{
	gtk_init(NULL, NULL);
	if (!argv[1])
	{
		DBusConnection *dbus;

		dbus = dbus_bus_get(DBUS_BUS_SYSTEM, NULL);
		dbus_connection_setup_with_g_main(dbus, g_main_context_default());
		init(dbus);
	} else /* Show ourselves if we have any argument. */
		timeout(init(NULL));

	gtk_main();
	return 0;
} /* main */
#endif /* ! HAVE_SYSTEMUI */

/* End of roadrunner.c */
