#include "openbox/action.h"
#include "openbox/action_list_run.h"
#include "openbox/config_value.h"
#include "openbox/misc.h"
#include "openbox/client.h"
#include "openbox/client_set.h"
#include "openbox/frame.h"
#include "openbox/screen.h"
#include <glib.h>

typedef struct {
    ObDirection dir;
    gboolean shrink;
} Options;

static gpointer setup_func(GHashTable *config);
static gpointer setup_shrink_func(GHashTable *config);
static void free_func(gpointer o);
static gboolean run_func(const ObClientSet *set,
                         const ObActionListRun *data, gpointer options);

void action_growtoedge_startup(void)
{
    action_register("GrowToEdge", OB_ACTION_DEFAULT_FILTER_SINGLE,
                    setup_func, free_func, run_func);
    action_register("ShrinkToEdge", OB_ACTION_DEFAULT_FILTER_SINGLE,
                    setup_shrink_func, free_func, run_func);
}

static gpointer setup_func(GHashTable *config)
{
    ObConfigValue *v;
    Options *o;

    o = g_slice_new0(Options);
    o->dir = OB_DIRECTION_NORTH;
    o->shrink = FALSE;

    v = g_hash_table_lookup(config, "direction");
    if (v && config_value_is_string(v)) {
        const gchar *s = config_value_string(v);
        if (!g_ascii_strcasecmp(s, "north") ||
            !g_ascii_strcasecmp(s, "up"))
            o->dir = OB_DIRECTION_NORTH;
        else if (!g_ascii_strcasecmp(s, "south") ||
                 !g_ascii_strcasecmp(s, "down"))
            o->dir = OB_DIRECTION_SOUTH;
        else if (!g_ascii_strcasecmp(s, "west") ||
                 !g_ascii_strcasecmp(s, "left"))
            o->dir = OB_DIRECTION_WEST;
        else if (!g_ascii_strcasecmp(s, "east") ||
                 !g_ascii_strcasecmp(s, "right"))
            o->dir = OB_DIRECTION_EAST;
    }

    return o;
}

static gpointer setup_shrink_func(GHashTable *config)
{
    Options *o;

    o = setup_func(config);
    o->shrink = TRUE;

    return o;
}

static gboolean do_grow(const ObActionListRun *data, gint x, gint y, gint w, gint h)
{
    gint realw, realh, lw, lh;

    realw = w;
    realh = h;
    client_try_configure(data->target, &x, &y, &realw, &realh,
                         &lw, &lh, TRUE);
    /* if it's going to be resized smaller than it intended, don't
       move the window over */
    if (x != data->target->area.x) x += w - realw;
    if (y != data->target->area.y) y += h - realh;

    if (x != data->target->area.x || y != data->target->area.y ||
        realw != data->target->area.width ||
        realh != data->target->area.height)
    {
        client_move_resize(data->target, x, y, realw, realh);
        return TRUE;
    }
    return FALSE;
}

static void free_func(gpointer o)
{
    g_slice_free(Options, o);
}

static gboolean each_run(ObClient *c,
                         const ObActionListRun *data, gpointer options)
{
    Options *o = options;
    gint x, y, w, h;
    ObDirection opp;
    gint half;

    if (!c ||
        /* don't allow vertical resize if shaded */
        ((o->dir == OB_DIRECTION_NORTH || o->dir == OB_DIRECTION_SOUTH) &&
         c->shaded))
    {
        return TRUE;
    }

    if (!o->shrink) {
        /* try grow */
        client_find_resize_directional(c, o->dir, TRUE, &x, &y, &w, &h);
        if (do_grow(data, x, y, w, h))
            return TRUE;
    }

    /* we couldn't grow, so try shrink! */
    opp = (o->dir == OB_DIRECTION_NORTH ? OB_DIRECTION_SOUTH :
           (o->dir == OB_DIRECTION_SOUTH ? OB_DIRECTION_NORTH :
            (o->dir == OB_DIRECTION_EAST ? OB_DIRECTION_WEST :
             OB_DIRECTION_EAST)));
    client_find_resize_directional(c, opp, FALSE, &x, &y, &w, &h);
    switch (opp) {
    case OB_DIRECTION_NORTH:
        half = c->area.y + c->area.height / 2;
        if (y > half) {
            h += y - half;
            y = half;
        }
        break;
    case OB_DIRECTION_SOUTH:
        half = c->area.height / 2;
        if (h < half)
            h = half;
        break;
    case OB_DIRECTION_WEST:
        half = c->area.x + c->area.width / 2;
        if (x > half) {
            w += x - half;
            x = half;
        }
        break;
    case OB_DIRECTION_EAST:
        half = c->area.width / 2;
        if (w < half)
            w = half;
        break;
    default: g_assert_not_reached();
    }
    if (do_grow(data, x, y, w, h))
        return TRUE;

    return TRUE;
}

/* Always return FALSE because its not interactive */
static gboolean run_func(const ObClientSet *set,
                         const ObActionListRun *data, gpointer options)
{
    if (!client_set_is_empty(set)) {
        action_client_move(data, TRUE);
        client_set_run(set, data, each_run, options);
        action_client_move(data, FALSE);
    }
    return FALSE;
}
