# Add Native Gradient Plotter Support

Add ifdef-based native gradient support. Source of truth is frontend-specific header. Initially enabled only for Qt.

## Proposed Changes

### Qt Frontend Feature Flag

#### [MODIFY] [misc.h](file:///mnt/netac/proj/neosurf/frontends/qt/misc.h)

Add feature flag (source of truth):
```c
/** Enable native gradient rendering via QLinearGradient */
#define NSQT_NATIVE_GRADIENTS 1
```

---

### Core Plotter API

#### [MODIFY] [plotters.h](file:///mnt/netac/proj/neosurf/include/neosurf/plotters.h)

Add gradient structures and optional callback:

```c
/** Linear gradient stop */
struct plot_gradient_stop {
    float offset;  /* 0.0 to 1.0 */
    colour color;
};

/** Linear gradient definition */
struct plot_linear_gradient {
    float x1, y1;  /* Start point */
    float x2, y2;  /* End point */
    struct plot_gradient_stop *stops;
    unsigned int stop_count;
};

/* In struct plotter_table: */
    /**
     * Fill a path with a linear gradient.
     * Optional, may be NULL.
     */
    nserror (*path_fill_gradient)(const struct redraw_context *ctx,
        const float *p, unsigned int n,
        const float transform[6],
        const struct plot_linear_gradient *gradient);
```

---

### Qt Backend Implementation

#### [MODIFY] [plotters.cpp](file:///mnt/netac/proj/neosurf/frontends/qt/plotters.cpp)

```cpp
#ifdef NSQT_NATIVE_GRADIENTS
static nserror nsqt_plot_path_fill_gradient(...) {
    // Build QPainterPath, create QLinearGradient, render
}
#endif

const struct plotter_table nsqt_plotters = {
    ...
#ifdef NSQT_NATIVE_GRADIENTS
    .path_fill_gradient = nsqt_plot_path_fill_gradient,
#else
    .path_fill_gradient = NULL,
#endif
};
```

---

### libsvgtiny Callback for Early Exit

#### [MODIFY] [svgtiny.h](file:///mnt/netac/proj/neosurf/contrib/libsvgtiny/include/svgtiny.h)

Add gradient callback type and setter:
```c
typedef enum {
    svgtiny_GRADIENT_FALLBACK,
    svgtiny_GRADIENT_NATIVE
} svgtiny_gradient_result;

typedef svgtiny_gradient_result (*svgtiny_gradient_callback)(
    void *ctx, float *path, unsigned int path_len,
    float x1, float y1, float x2, float y2,
    svgtiny_colour *colors, float *offsets, unsigned int stop_count);

void svgtiny_set_gradient_callback(struct svgtiny_diagram *diagram,
    svgtiny_gradient_callback cb, void *ctx);
```

#### [MODIFY] [svgtiny_gradient.c](file:///mnt/netac/proj/neosurf/contrib/libsvgtiny/src/svgtiny_gradient.c)

In `svgtiny_gradient_add_fill_path()`:
```c
/* Check for native gradient handler FIRST */
if (state->gradient_callback != NULL) {
    result = state->gradient_callback(...);
    if (result == svgtiny_GRADIENT_NATIVE) {
        state->fill = svgtiny_TRANSPARENT;
        return svgtiny_OK;  /* Skip tessellation! */
    }
}
/* Existing tessellation code below... */
```

---

### SVG Content Handler

#### [MODIFY] [svg.c](file:///mnt/netac/proj/neosurf/src/content/handlers/image/svg.c)

Register callback before parsing:
```c
static svgtiny_gradient_result svg_gradient_callback(...) {
    struct svg_context *svg = ctx;
    if (svg->redraw_ctx->plot->path_fill_gradient != NULL) {
        /* Build plot_linear_gradient, call plotter */
        return svgtiny_GRADIENT_NATIVE;
    }
    return svgtiny_GRADIENT_FALLBACK;
}

/* Before svgtiny_parse(): */
svgtiny_set_gradient_callback(diagram, svg_gradient_callback, svg_ctx);
```

---

## Summary

| Component | Change |
|-----------|--------|
| `qt/misc.h` | Define `NSQT_NATIVE_GRADIENTS` |
| `plotters.h` | Add gradient structs + optional callback |
| `qt/plotters.cpp` | Implement with QLinearGradient (#ifdef) |
| `svgtiny.h` | Add callback type |
| `svgtiny_gradient.c` | Check callback first, skip tessellation |
| `svg.c` | Register callback, invoke plotter |

**Result:** Qt gets smooth gradients, other frontends unchanged, zero tessellation overhead when native support exists.
