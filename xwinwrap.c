/*
 * Copyright © 2005 Novell, Inc.
 *
 * Permission to use, copy, modify, distribute, and sell this software
 * and its documentation for any purpose is hereby granted without
 * fee, provided that the above copyright notice appear in all copies
 * and that both that copyright notice and this permission notice
 * appear in supporting documentation, and that the name of
 * Novell, Inc. not be used in advertising or publicity pertaining to
 * distribution of the software without specific, written prior permission.
 * Novell, Inc. makes no representations about the suitability of this
 * software for any purpose. It is provided "as is" without express or
 * implied warranty.
 *
 * NOVELL, INC. DISCLAIMS ALL WARRANTIES WITH REGARD TO THIS SOFTWARE,
 * INCLUDING ALL IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS, IN
 * NO EVENT SHALL NOVELL, INC. BE LIABLE FOR ANY SPECIAL, INDIRECT OR
 * CONSEQUENTIAL DAMAGES OR ANY DAMAGES WHATSOEVER RESULTING FROM LOSS
 * OF USE, DATA OR PROFITS, WHETHER IN AN ACTION OF CONTRACT,
 * NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF OR IN CONNECTION
 * WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 *
 * Author: David Reveman <davidr@novell.com>
 */

/*
 * Modified by: Max van Deurzen
 * Homepage: https://github.com/deurzen
 */

#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/Xatom.h>
#include <X11/Xproto.h>

#include <X11/extensions/shape.h>
#include <X11/extensions/Xrender.h>

#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <stdio.h>
#include <unistd.h>
#include <sys/wait.h>
#include <sys/stat.h>
#include <sys/types.h>

typedef enum {
    SHAPE_RECT = 0,
    SHAPE_CIRCLE,
    SHAPE_TRIG,
} win_shape;

static pid_t pid = 0;

static char** childArgv = 0;
static int nChildArgv = 0;
int debug = 0;

static int
addArguments(char** argv, int n)
{
    char** newArgv;
    int i;

    newArgv = realloc(childArgv, sizeof(char*) * (nChildArgv + n));
    if (!newArgv)
        return 0;

    for (i = 0; i < n; i++)
        newArgv[nChildArgv + i] = argv[i];

    childArgv = newArgv;
    nChildArgv += n;

    return n;
}

static void
sigHandler(int sig)
{
    kill(pid, sig);
}

static Window
find_desktop_window(Display* display, int screen, Window* root, Window* p_desktop)
{
    int i;
    unsigned int n;
    Window win = *root;
    Window troot, parent, *children;
    char* name;
    int status;
    int width = DisplayWidth(display, screen);
    int height = DisplayHeight(display, screen);
    XWindowAttributes attrs;

    XQueryTree(display, *root, &troot, &parent, &children, &n);
    for (i = 0; i < (int)n; i++) {
        status = XFetchName(display, children[i], &name);
        status |= XGetWindowAttributes(display, children[i], &attrs);
        if ((status != 0) && (NULL != name)) {
            if ((attrs.map_state != 0) && (attrs.width == width) && (attrs.height == height))
            {
                win = children[i];
                XFree(children);
                XFree(name);
                *p_desktop = win;
                return win;
            }
            if (name) {
                XFree(name);
            }
        }
    }
    return 0;
}

int
main(int argc, char** argv)
{
    Display* dpy;
    Window win;
    Window root;
    Window p_desktop = 0;
    int screen;
    XSizeHints xsh;
    XWMHints xwmh;
    char widArg[256];
    char* widArgv[] = { widArg };
    char* endArg = NULL;
    int i;
    int status = 0;
    int x;
    int y;
    unsigned int width;
    unsigned int height;

    dpy = XOpenDisplay(NULL);
    if (!dpy) {
        fprintf(stderr, "%s: could not open display\n", argv[0]);
        return 1;
    }

    screen = DefaultScreen(dpy);
    root = RootWindow(dpy, screen);

    XWindowAttributes root_attrs;
    XGetWindowAttributes(dpy, root, &root_attrs);
    x = root_attrs.x;
    y = root_attrs.y;
    width = root_attrs.width;
    height = root_attrs.height;

    for (i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-g") == 0) {
            if (++i < argc)
                XParseGeometry(argv[i], &x, &y, &width, &height);
        } else if (strcmp(argv[i], "--") == 0) {
            break;
        } else {
            return 1;
        }
    }

    for (i = i + 1; i < argc; i++) {
        if (strcmp(argv[i], "WID") == 0)
            addArguments(widArgv, 1);
        else
            addArguments(&argv[i], 1);
    }

    if (!nChildArgv) {
        fprintf(stderr, "%s: could not create command line\n", argv[0]);
        return 1;
    }

    addArguments(&endArg, 1);

    xsh.flags = PSize;
    xsh.width = width;
    xsh.height = height;

    xwmh.flags = InputHint;
    xwmh.input = 0;

    XSetWindowAttributes attr;
    attr.override_redirect = 1;

    if (win == root) {
        { // override-redirect
            if (find_desktop_window(dpy, screen, &root, &p_desktop)) {
                win = XCreateWindow(dpy, p_desktop, x, y, xsh.width, xsh.height, 0,
                    CopyFromParent, InputOutput, CopyFromParent, CWOverrideRedirect, &attr);
            } else {
                win = XCreateWindow(dpy, root, x, y, xsh.width, xsh.height, 0, CopyFromParent,
                    InputOutput, CopyFromParent, CWOverrideRedirect, &attr);
            }
        }

        XSetWMProperties(dpy, win, NULL, NULL, argv, argc, &xsh, &xwmh, NULL);

        { // noinput
            Region region;
            region = XCreateRegion();
            if (region) {
                XShapeCombineRegion(dpy, win, ShapeInput, 0, 0, region, ShapeSet);
                XDestroyRegion(region);
            }
        }

        { // below state
            Atom window_type = XInternAtom(dpy, "_NET_WM_STATE", False);
            Atom desktop = XInternAtom(dpy, "_NET_WM_STATE_BELOW", False);
            XChangeProperty(dpy, win, window_type, XA_ATOM, 32, PropModeReplace,
                (unsigned char*)&desktop, 1);
        }

        XMapWindow(dpy, win);
    }

    XSync(dpy, win);
    sprintf(widArg, "0x%x", (int)win);

    pid = fork();
    switch (pid) {
        case -1:
            perror("fork");
            return 1;
        case 0:
            execvp(childArgv[0], childArgv);
            perror(childArgv[0]);
            exit(2);
            break;
        default:
            break;
    }

    signal(SIGTERM, sigHandler);
    signal(SIGINT, sigHandler);

    for (;;) {
        if (waitpid(pid, &status, 0) != -1) {
            if (WIFEXITED(status))
                fprintf(stderr, "%s died, exit status %d\n", childArgv[0],
                    WEXITSTATUS(status));

            break;
        }
    }

    XDestroyWindow(dpy, win);
    XCloseDisplay(dpy);

    return 0;
}
