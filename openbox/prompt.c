/* -*- indent-tabs-mode: nil; tab-width: 4; c-basic-offset: 4; -*-

   prompt.c for the Openbox window manager
   Copyright (c) 2008        Dana Jansens

   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; either version 2 of the License, or
   (at your option) any later version.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   See the COPYING file for a copy of the GNU General Public License.
*/

#include "prompt.h"
#include "openbox.h"
#include "screen.h"
#include "openbox.h"
#include "client.h"
#include "prop.h"
#include "modkeys.h"
#include "gettext.h"

static GList *prompt_list = NULL;

/* we construct these */
static RrAppearance *prompt_a_button;
static RrAppearance *prompt_a_focus;
static RrAppearance *prompt_a_press;
/* we change the max width which would screw with others */
static RrAppearance *prompt_a_msg;

static void prompt_layout(ObPrompt *self);
static void render_all(ObPrompt *self);
static void render_button(ObPrompt *self, ObPromptElement *e);

void prompt_startup(gboolean reconfig)
{
    RrColor *c_button, *c_focus, *c_press;

    prompt_a_button = RrAppearanceCopy(ob_rr_theme->a_focused_unpressed_close);
    prompt_a_focus = RrAppearanceCopy(ob_rr_theme->a_hover_focused_close);
    prompt_a_press = RrAppearanceCopy(ob_rr_theme->a_focused_pressed_close);

    c_button = prompt_a_button->texture[0].data.mask.color;
    c_focus = prompt_a_button->texture[0].data.mask.color;
    c_press = prompt_a_button->texture[0].data.mask.color;

    RrAppearanceRemoveTextures(prompt_a_button);
    RrAppearanceRemoveTextures(prompt_a_focus);
    RrAppearanceRemoveTextures(prompt_a_press);

    RrAppearanceAddTextures(prompt_a_button, 1);
    RrAppearanceAddTextures(prompt_a_focus, 1);
    RrAppearanceAddTextures(prompt_a_press, 1);

    /* totally cheating here.. */
    prompt_a_button->texture[0] = ob_rr_theme->osd_hilite_label->texture[0];
    prompt_a_focus->texture[0] = ob_rr_theme->osd_hilite_label->texture[0];
    prompt_a_press->texture[0] = ob_rr_theme->osd_hilite_label->texture[0];

    prompt_a_button->texture[0].data.text.justify = RR_JUSTIFY_CENTER;
    prompt_a_focus->texture[0].data.text.justify = RR_JUSTIFY_CENTER;
    prompt_a_press->texture[0].data.text.justify = RR_JUSTIFY_CENTER;

    prompt_a_button->texture[0].data.text.color = c_button;
    prompt_a_focus->texture[0].data.text.color = c_focus;
    prompt_a_press->texture[0].data.text.color = c_press;

    prompt_a_msg = RrAppearanceCopy(ob_rr_theme->osd_hilite_label);
    prompt_a_msg->texture[0].data.text.flow = TRUE;

    if (reconfig) {
        GList *it;
        for (it = prompt_list; it; it = g_list_next(it)) {
            ObPrompt *p = it->data;
            prompt_layout(p);
            render_all(p);
        }
    }
}

void prompt_shutdown(gboolean reconfig)
{
    RrAppearanceFree(prompt_a_button);
    RrAppearanceFree(prompt_a_focus);
    RrAppearanceFree(prompt_a_press);
    RrAppearanceFree(prompt_a_msg);
}

ObPrompt* prompt_new(const gchar *msg, const gchar *const *answers)
{
    ObPrompt *self;
    XSetWindowAttributes attrib;
    guint i;
    const gchar *const *c;

    attrib.override_redirect = FALSE;
    attrib.border_pixel = RrColorPixel(ob_rr_theme->osd_border_color);

    self = g_new0(ObPrompt, 1);
    self->ref = 1;
    self->super.type = Window_Prompt;
    self->super.window = XCreateWindow(ob_display,
                                       RootWindow(ob_display, ob_screen),
                                       0, 0, 1, 1, ob_rr_theme->obwidth,
                                       CopyFromParent, InputOutput,
                                       CopyFromParent,
                                       CWOverrideRedirect | CWBorderPixel,
                                       &attrib);

    /* make it a dialog type window */
    PROP_SET32(self->super.window, net_wm_window_type, atom,
               prop_atoms.net_wm_window_type_dialog);

    /* listen for key presses on the window */
    self->event_mask = KeyPressMask;

    self->a_bg = RrAppearanceCopy(ob_rr_theme->osd_hilite_bg);

    self->msg.text = g_strdup(msg);
    self->msg.window = XCreateWindow(ob_display, self->super.window,
                                     0, 0, 1, 1, 0,
                                     CopyFromParent, InputOutput,
                                     CopyFromParent, 0, NULL);
    XMapWindow(ob_display, self->msg.window);

    self->n_buttons = 0;
    for (c = answers; *c != NULL; ++c)
        ++self->n_buttons;

    if (!self->n_buttons)
        self->n_buttons = 1;

    self->button = g_new(ObPromptElement, self->n_buttons);

    if (!answers) {
        g_assert(self->n_buttons == 1); /* should be set to this above.. */
        self->button[0].text = g_strdup(_("OK"));
    }
    else {
        g_assert(self->n_buttons > 0);
        for (i = 0; i < self->n_buttons; ++i)
            self->button[i].text = g_strdup(answers[i]);
    }

    for (i = 0; i < self->n_buttons; ++i) {
        self->button[i].window = XCreateWindow(ob_display, self->super.window,
                                               0, 0, 1, 1, 0,
                                               CopyFromParent, InputOutput,
                                               CopyFromParent, 0, NULL);
        XMapWindow(ob_display, self->button[i].window);
        g_hash_table_insert(window_map, &self->button[i].window,
                            PROMPT_AS_WINDOW(self));

        /* listen for button presses on the buttons */
        XSelectInput(ob_display, self->button[i].window,
                     ButtonPressMask | ButtonReleaseMask | ButtonMotionMask);
    }

    /* set the focus to the first button */
    self->focus = &self->button[0];

    prompt_list = g_list_prepend(prompt_list, self);

    return self;
}

void prompt_ref(ObPrompt *self)
{
    ++self->ref;
}

void prompt_unref(ObPrompt *self)
{
    if (self && --self->ref == 0) {
        guint i;

        prompt_list = g_list_remove(prompt_list, self);

        for (i = 0; i < self->n_buttons; ++i) {
            g_hash_table_remove(window_map, &self->button[i].window);
            XDestroyWindow(ob_display, self->button[i].window);
        }

        XDestroyWindow(ob_display, self->msg.window);

        RrAppearanceFree(self->a_bg);

        XDestroyWindow(ob_display, self->super.window);
        g_free(self);
    }
}

static void prompt_layout(ObPrompt *self)
{
    gint l, r, t, b;
    guint i;
    gint allbuttonsw, allbuttonsh, buttonx;
    gint w, h;
    gint maxw;

    const gint OUTSIDE_MARGIN = 4;
    const gint MSG_BUTTON_SEPARATION = 4;
    const gint BUTTON_SEPARATION = 4;
    const gint BUTTON_VMARGIN = 4;
    const gint BUTTON_HMARGIN = 12;
    const gint MAX_WIDTH = 600;

    RrMargins(self->a_bg, &l, &t, &r, &b);
    l += OUTSIDE_MARGIN;
    t += OUTSIDE_MARGIN;
    r += OUTSIDE_MARGIN;
    b += OUTSIDE_MARGIN;

    {
        Rect *area = screen_physical_area_all_monitors();
        maxw = MIN(MAX_WIDTH, area->width*4/5);
        g_free(area);
    }

    /* find the button sizes and how much space we need for them */
    allbuttonsw = allbuttonsh = 0;
    for (i = 0; i < self->n_buttons; ++i) {
        gint bw, bh;

        prompt_a_button->texture[0].data.text.string = self->button[i].text;
        prompt_a_focus->texture[0].data.text.string = self->button[i].text;
        prompt_a_press->texture[0].data.text.string = self->button[i].text;
        RrMinSize(prompt_a_button, &bw, &bh);
        self->button[i].width = bw;
        self->button[i].height = bh;
        RrMinSize(prompt_a_focus, &bw, &bh);
        self->button[i].width = MAX(self->button[i].width, bw);
        self->button[i].height = MAX(self->button[i].height, bh);
        RrMinSize(prompt_a_press, &bw, &bh);
        self->button[i].width = MAX(self->button[i].width, bw);
        self->button[i].height = MAX(self->button[i].height, bh);

        self->button[i].width += BUTTON_HMARGIN * 2;
        self->button[i].height += BUTTON_VMARGIN * 2;

        allbuttonsw += self->button[i].width + (i > 0 ? BUTTON_SEPARATION : 0);
        allbuttonsh = MAX(allbuttonsh, self->button[i].height);
    }

    self->msg_wbound = MAX(allbuttonsw, maxw);

    /* measure the text message area */
    prompt_a_msg->texture[0].data.text.string = self->msg.text;
    prompt_a_msg->texture[0].data.text.maxwidth = self->msg_wbound;
    RrMinSize(prompt_a_msg, &self->msg.width, &self->msg.height);

    /* width and height inside the outer margins */
    w = MAX(self->msg.width, allbuttonsw);
    h = self->msg.height + MSG_BUTTON_SEPARATION + allbuttonsh;

    /* position the text message */
    self->msg.x = l + (w - self->msg.width) / 2;
    self->msg.y = t;

    /* position the button buttons */
    buttonx = l + (w - allbuttonsw) / 2;
    for (i = 0; i < self->n_buttons; ++i) {
        self->button[i].x = buttonx;
        buttonx += self->button[i].width + BUTTON_SEPARATION;
        self->button[i].y = t + h - allbuttonsh;
        self->button[i].y += (allbuttonsh - self->button[i].height) / 2;
    }

    /* size and position the toplevel window */
    self->width = w + l + r;
    self->height = h + t + b;

    /* move and resize the actual windows */
    XResizeWindow(ob_display, self->super.window, self->width, self->height);
    XMoveResizeWindow(ob_display, self->msg.window,
                      self->msg.x, self->msg.y,
                      self->msg.width, self->msg.height);
    for (i = 0; i < self->n_buttons; ++i)
        XMoveResizeWindow(ob_display, self->button[i].window,
                          self->button[i].x, self->button[i].y,
                          self->button[i].width, self->button[i].height);
}

static void render_button(ObPrompt *self, ObPromptElement *e)
{
    RrAppearance *a;

    if (e->pressed) a = prompt_a_press;
    else if (self->focus == e) a = prompt_a_focus;
    else a = prompt_a_button;

    a->surface.parent = self->a_bg;
    a->surface.parentx = e->x;
    a->surface.parentx = e->y;

    a->texture[0].data.text.string = e->text;
    RrPaint(a, e->window, e->width, e->height);
}

static void render_all(ObPrompt *self)
{
    guint i;

    RrPaint(self->a_bg, self->super.window, self->width, self->height);

    prompt_a_msg->surface.parent = self->a_bg;
    prompt_a_msg->surface.parentx = self->msg.x;
    prompt_a_msg->surface.parenty = self->msg.y;

    prompt_a_msg->texture[0].data.text.string = self->msg.text;
    prompt_a_msg->texture[0].data.text.maxwidth = self->msg_wbound;
    RrPaint(prompt_a_msg, self->msg.window, self->msg.width, self->msg.height);

    for (i = 0; i < self->n_buttons; ++i)
        render_button(self, &self->button[i]);
}

void prompt_show(ObPrompt *self, ObClient *parent)
{
    XSizeHints hints;

    if (self->mapped) return;

    prompt_layout(self);
    render_all(self);

    /* you can't resize the prompt */
    hints.flags = PMinSize | PMaxSize;
    hints.min_width = hints.max_width = self->width;
    hints.min_height = hints.max_height = self->height;
    XSetWMNormalHints(ob_display, self->super.window, &hints);

    XSetTransientForHint(ob_display, (parent ? parent->window : 0),
                         self->super.window);

    client_manage(self->super.window, self);

    self->mapped = TRUE;
}

void prompt_hide(ObPrompt *self)
{
    XUnmapWindow(ob_display, self->super.window);
    self->mapped = FALSE;
}

void prompt_key_event(ObPrompt *self, XEvent *e)
{
    gboolean shift;
    guint shift_mask;

    if (e->type != KeyPress) return;

    g_print("key 0x%x 0x%x\n", e->xkey.keycode, ob_keycode(OB_KEY_TAB));

    shift_mask = modkeys_key_to_mask(OB_MODKEY_KEY_SHIFT);
    shift = !!(e->xkey.state & shift_mask);

    /* only accept shift */
    if (e->xkey.state != 0 && e->xkey.state != shift_mask)
        return;

    if (e->xkey.keycode == ob_keycode(OB_KEY_ESCAPE))
        prompt_hide(self);
    else if (e->xkey.keycode == ob_keycode(OB_KEY_RETURN)) {
        /* XXX run stuff */
        prompt_hide(self);
    }
    else if (e->xkey.keycode == ob_keycode(OB_KEY_TAB)) {
        guint i;
        ObPromptElement *oldfocus;

        oldfocus = self->focus;

        for (i = 0; i < self->n_buttons; ++i)
            if (self->focus == &self->button[i]) break;
        i += (shift ? -1 : 1);
        if (i < 0) i = self->n_buttons - 1;
        else if (i >= self->n_buttons) i = 0;
        self->focus = &self->button[i];

        if (oldfocus != self->focus) render_button(self, oldfocus);
        render_button(self, self->focus);
    }
}

void prompt_mouse_event(ObPrompt *self, XEvent *e)
{
    guint i;
    ObPromptElement *but;

    if (e->type != ButtonPress && e->type != ButtonRelease &&
        e->type != MotionNotify) return;

    /* find the button */
    for (i = 0; i < self->n_buttons; ++i)
        if (self->button[i].window ==
            (e->type == MotionNotify ? e->xmotion.window : e->xbutton.window))
        {
            but = &self->button[i];
            break;
        }
    g_assert(but != NULL);

    if (e->type == ButtonPress) {
        ObPromptElement *oldfocus;

        oldfocus = self->focus;

        but->pressed = TRUE;
        self->focus = but;

        if (oldfocus != but) render_button(self, oldfocus);
        render_button(self, but);
    }
    else if (e->type == ButtonRelease) {
        if (but->pressed) {
            /* XXX run stuff */
            prompt_hide(self);
        }
    }
    else if (e->type == MotionNotify) {
        gboolean press;

        press = (e->xmotion.x >= 0 && e->xmotion.y >= 0 &&
                 e->xmotion.x < but->width && e->xmotion.y < but->height);

        if (press != but->pressed) {
            but->pressed = press;
            render_button(self, but);
        }
    }
}
