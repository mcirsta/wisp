/*
 * This file is part of Libsvgtiny
 * Licensed under the MIT License,
 *                http://opensource.org/licenses/mit-license.php
 * Copyright 2024 Vincent Sanders <vince@netsurf-browser.org>
 */

#include <stddef.h>
#include <math.h>
#include <stdlib.h>

#include "svgtiny.h"
#include "svgtiny_internal.h"
#include "svgtiny_parse.h"

#define TAU             6.28318530717958647692

#ifndef M_PI
#define M_PI		3.14159265358979323846
#endif

#ifndef M_PI_2
#define M_PI_2          1.57079632679489661923
#endif

#define degToRad(angleInDegrees) ((angleInDegrees) * M_PI / 180.0)
#define radToDeg(angleInRadians) ((angleInRadians) * 180.0 / M_PI)


struct internal_points {
	float *p; /* points */
	unsigned int used; /* number of used points */
	size_t alloc; /* number of allocated points */
};

/**
 * internal path representation
 */
struct internal_path_state {
	struct internal_points path;/* generated path */
	struct internal_points cmd; /* parameters to current command */
	float prev_x; /* previous x coordinate */
	float prev_y; /* previous y coordinate */
	float subpath_x; /* subpath start x coordinate */
	float subpath_y; /* subpath start y coordinate */
	float cubic_x; /* previous cubic x control point */
	float cubic_y; /* previous cubic y control point */
	float quad_x; /* previous quadratic x control point */
	float quad_y; /* previous quadratic y control point */
};

/**
 * ensure there is enough storage allocated to make points available
 *
 * \param ipts internal points to enaure space in
 * \param count The number of points of space to ensure.
 * \return svgtiny_OK on success else svgtiny_OUT_OF_MEMORY if allocation fails
 */
static inline svgtiny_code
ensure_internal_points(struct internal_points *ipts, unsigned int count)
{
	svgtiny_code res = svgtiny_OK;
	float *nalloc;

	if ((ipts->used + count) > ipts->alloc) {
		nalloc = realloc(ipts->p, sizeof ipts->p[0] * (ipts->alloc + 84));
		if (nalloc == NULL) {
			return svgtiny_OUT_OF_MEMORY;
		}
		ipts->p = nalloc;
		ipts->alloc = ipts->alloc + 84;
	}
	return res;
}

/**
 * moveto
 *
 * command and parameters: M|m (x, y)+
 */
static inline svgtiny_code
generate_path_move(struct internal_path_state *state, int relative)
{
	svgtiny_code res;
	unsigned int cmdpc = 0; /* command parameter count */

	if ((state->cmd.used < 2) || ((state->cmd.used % 2) != 0)) {
		/* wrong number of parameters */
		return svgtiny_SVG_ERROR;
	}

	for (cmdpc = 0; cmdpc < state->cmd.used; cmdpc += 2) {
		res = ensure_internal_points(&state->path, 3);
		if (res != svgtiny_OK) {
			return res;
		}

		if (relative != 0) {
			state->cmd.p[cmdpc] += state->prev_x;
			state->cmd.p[cmdpc + 1] += state->prev_y;
		}

		if (cmdpc == 0) {
			state->path.p[state->path.used++] = svgtiny_PATH_MOVE;
			/* the move starts a subpath */
			state->path.p[state->path.used++] =
				state->subpath_x =
				state->cubic_x =
				state->prev_x = state->cmd.p[cmdpc];
			state->path.p[state->path.used++] =
				state->subpath_y =
				state->cubic_y =
				state->prev_y = state->cmd.p[cmdpc + 1];
		} else {
			state->path.p[state->path.used++] = svgtiny_PATH_LINE;

			state->path.p[state->path.used++] =
				state->cubic_x =
				state->quad_x =
				state->prev_x = state->cmd.p[cmdpc];
			state->path.p[state->path.used++] =
				state->cubic_y =
				state->quad_y =
				state->prev_y = state->cmd.p[cmdpc + 1];
		}

	}
	return svgtiny_OK;
}

/**
 * closepath
 *
 * command and parameters: Z|z
 */
static inline svgtiny_code
generate_path_close(struct internal_path_state *state)
{
	svgtiny_code res;

	if (state->cmd.used != 0) {
		/* wrong number of parameters */
		return svgtiny_SVG_ERROR;
	}

	res = ensure_internal_points(&state->path, 1);
	if (res != svgtiny_OK) {
		return res;
	}

	state->path.p[state->path.used++] = svgtiny_PATH_CLOSE;

	state->cubic_x = state->quad_x = state->prev_x = state->subpath_x;
	state->cubic_y = state->quad_y = state->prev_y = state->subpath_y;

	return svgtiny_OK;
}


/**
 * lineto
 *
 * command and parameters: L|l (x, y)+
 */
static inline svgtiny_code
generate_path_line(struct internal_path_state *state, int relative)
{
	svgtiny_code res;
	unsigned int cmdpc = 0; /* command parameter count */

	if ((state->cmd.used < 2) || ((state->cmd.used % 2) != 0)) {
		/* wrong number of parameters */
		return svgtiny_SVG_ERROR;
	}

	for (cmdpc = 0; cmdpc < state->cmd.used; cmdpc += 2) {
		res = ensure_internal_points(&state->path, 3);
		if (res != svgtiny_OK) {
			return res;
		}

		if (relative != 0) {
			state->cmd.p[cmdpc] += state->prev_x;
			state->cmd.p[cmdpc + 1] += state->prev_y;
		}
		state->path.p[state->path.used++] = svgtiny_PATH_LINE;
		state->path.p[state->path.used++] =
			state->cubic_x =
			state->quad_x =
			state->prev_x = state->cmd.p[cmdpc];
		state->path.p[state->path.used++] =
			state->cubic_y =
			state->quad_y =
			state->prev_y = state->cmd.p[cmdpc + 1];
	}
	return svgtiny_OK;
}


/**
 * horizontal lineto
 *
 * command and parameters: H|h x+
 */
static inline svgtiny_code
generate_path_hline(struct internal_path_state *state, int relative)
{
	svgtiny_code res;
	unsigned int cmdpc = 0; /* command parameter count */

	if (state->cmd.used < 1) {
		/* wrong number of parameters */
		return svgtiny_SVG_ERROR;
	}

	for (cmdpc = 0; cmdpc < state->cmd.used; cmdpc++) {
		res = ensure_internal_points(&state->path, 3);
		if (res != svgtiny_OK) {
			return res;
		}

		if (relative != 0) {
			state->cmd.p[cmdpc] += state->prev_x;
		}
		state->path.p[state->path.used++] = svgtiny_PATH_LINE;
		state->path.p[state->path.used++] =
			state->cubic_x =
			state->quad_x =
			state->prev_x = state->cmd.p[cmdpc];
		state->path.p[state->path.used++] =
			state->cubic_y =
			state->quad_x = state->prev_y;
	}
	return svgtiny_OK;
}

/**
 * vertical lineto
 *
 * command and parameters: V|v y+
 */
static inline svgtiny_code
generate_path_vline(struct internal_path_state *state, int relative)
{
	svgtiny_code res;
	unsigned int cmdpc = 0; /* command parameter count */

	if (state->cmd.used < 1) {
		/* wrong number of parameters */
		return svgtiny_SVG_ERROR;
	}

	for (cmdpc = 0; cmdpc < state->cmd.used; cmdpc++) {
		res = ensure_internal_points(&state->path, 3);
		if (res != svgtiny_OK) {
			return res;
		}

		if (relative != 0) {
			state->cmd.p[cmdpc] += state->prev_y;
		}
		state->path.p[state->path.used++] = svgtiny_PATH_LINE;
		state->path.p[state->path.used++] =
			state->cubic_x =
			state->quad_x = state->prev_x;
		state->path.p[state->path.used++] =
			state->cubic_y =
			state->quad_y =
			state->prev_y = state->cmd.p[cmdpc];
	}
	return svgtiny_OK;
}

/**
 * curveto
 *
 * command and parameters: C|c (x1, y1, x2, y2, x, y)+
 */
static inline svgtiny_code
generate_path_curveto(struct internal_path_state *state, int relative)
{
	svgtiny_code res;
	unsigned int cmdpc = 0; /* command parameter count */

	if ((state->cmd.used < 6) || ((state->cmd.used % 6) != 0)) {
		/* wrong number of parameters */
		return svgtiny_SVG_ERROR;
	}

	for (cmdpc = 0; cmdpc < state->cmd.used; cmdpc += 6) {
		res = ensure_internal_points(&state->path, 7);
		if (res != svgtiny_OK) {
			return res;
		}

		if (relative != 0) {
			state->cmd.p[cmdpc + 0] += state->prev_x; /* x1 */
			state->cmd.p[cmdpc + 1] += state->prev_y; /* y1 */
			state->cmd.p[cmdpc + 2] += state->prev_x; /* x2 */
			state->cmd.p[cmdpc + 3] += state->prev_y; /* y2 */
			state->cmd.p[cmdpc + 4] += state->prev_x; /* x */
			state->cmd.p[cmdpc + 5] += state->prev_y; /* y */
		}

		state->path.p[state->path.used++] = svgtiny_PATH_BEZIER;
		state->path.p[state->path.used++] = state->cmd.p[cmdpc + 0];
		state->path.p[state->path.used++] = state->cmd.p[cmdpc + 1];
		state->path.p[state->path.used++] =
			state->cubic_x = state->cmd.p[cmdpc + 2];
		state->path.p[state->path.used++] =
			state->cubic_y = state->cmd.p[cmdpc + 3];
		state->path.p[state->path.used++] =
			state->quad_x = state->prev_x = state->cmd.p[cmdpc + 4];
		state->path.p[state->path.used++] =
			state->quad_y = state->prev_y = state->cmd.p[cmdpc + 5];
	}
	return svgtiny_OK;
}

/**
 * smooth curveto
 *
 * command and parameters: S|s (x2, y2, x, y)+
 */
static inline svgtiny_code
generate_path_scurveto(struct internal_path_state *state, int relative)
{
	svgtiny_code res;
	unsigned int cmdpc = 0; /* command parameter count */

	if ((state->cmd.used < 4) || ((state->cmd.used % 4) != 0)) {
		/* wrong number of parameters */
		return svgtiny_SVG_ERROR;
	}

	for (cmdpc = 0; cmdpc < state->cmd.used; cmdpc += 4) {
		float x1;
		float y1;

		res = ensure_internal_points(&state->path, 7);
		if (res != svgtiny_OK) {
			return res;
		}

		x1 = state->prev_x + (state->prev_x - state->cubic_x);
		y1 = state->prev_y + (state->prev_y - state->cubic_y);
		if (relative != 0) {
			state->cmd.p[cmdpc + 0] += state->prev_x; /* x2 */
			state->cmd.p[cmdpc + 1] += state->prev_y; /* y2 */
			state->cmd.p[cmdpc + 2] += state->prev_x; /* x */
			state->cmd.p[cmdpc + 3] += state->prev_y; /* y */
		}

		state->path.p[state->path.used++] = svgtiny_PATH_BEZIER;
		state->path.p[state->path.used++] = x1;
		state->path.p[state->path.used++] = y1;
		state->path.p[state->path.used++] =
			state->cubic_x = state->cmd.p[cmdpc + 0];
		state->path.p[state->path.used++] =
			state->cubic_x = state->cmd.p[cmdpc + 1];
		state->path.p[state->path.used++] =
			state->quad_x =
			state->prev_x = state->cmd.p[cmdpc + 2];
		state->path.p[state->path.used++] =
			state->quad_y =
			state->prev_y = state->cmd.p[cmdpc + 3];
	}
	return svgtiny_OK;
}

/**
 * quadratic Bezier curveto
 *
 * command and parameters: Q|q (x1, y1, x ,y)+
 */
static inline svgtiny_code
generate_path_bcurveto(struct internal_path_state *state, int relative)
{
	svgtiny_code res;
	unsigned int cmdpc = 0; /* command parameter count */

	if ((state->cmd.used < 4) || ((state->cmd.used % 4) != 0)) {
		/* wrong number of parameters */
		return svgtiny_SVG_ERROR;
	}

	for (cmdpc = 0; cmdpc < state->cmd.used; cmdpc += 4) {
		res = ensure_internal_points(&state->path, 7);
		if (res != svgtiny_OK) {
			return res;
		}

		state->quad_x = state->cmd.p[cmdpc + 0] /* x1 */;
		state->quad_y = state->cmd.p[cmdpc + 1] /* y1 */;
		if (relative != 0) {
			state->cmd.p[cmdpc + 0] += state->prev_x; /* x1 */
			state->cmd.p[cmdpc + 1] += state->prev_y; /* y1 */
			state->cmd.p[cmdpc + 2] += state->prev_x; /* x */
			state->cmd.p[cmdpc + 3] += state->prev_y; /* y */
		}

		state->path.p[state->path.used++] = svgtiny_PATH_BEZIER;
		state->path.p[state->path.used++] =
			1./3 * state->prev_x +
			2./3 * state->cmd.p[cmdpc + 0];
		state->path.p[state->path.used++] =
			1./3 * state->prev_y +
			2./3 * state->cmd.p[cmdpc + 1];
		state->path.p[state->path.used++] =
			2./3 * state->cmd.p[cmdpc + 0] +
			1./3 * state->cmd.p[cmdpc + 2];
		state->path.p[state->path.used++] =
			2./3 * state->cmd.p[cmdpc + 1] +
			1./3 * state->cmd.p[cmdpc + 3];
		state->path.p[state->path.used++] =
			state->cubic_x =
			state->prev_x = state->cmd.p[cmdpc + 2];
		state->path.p[state->path.used++] =
			state->cubic_y =
			state->prev_y = state->cmd.p[cmdpc + 3];
	}
	return svgtiny_OK;
}

/**
 * smooth quadratic Bezier curveto
 *
 * command and parameters: T|t (x ,y)+
 */
static inline svgtiny_code
generate_path_sbcurveto(struct internal_path_state *state, int relative)
{
	svgtiny_code res;
	unsigned int cmdpc = 0; /* command parameter count */

	if ((state->cmd.used < 2) || ((state->cmd.used % 2) != 0)) {
		/* wrong number of parameters */
		return svgtiny_SVG_ERROR;
	}

	for (cmdpc = 0; cmdpc < state->cmd.used; cmdpc += 2) {
		float x1;
		float y1;

		res = ensure_internal_points(&state->path, 7);
		if (res != svgtiny_OK) {
			return res;
		}

		x1 = state->prev_x + (state->prev_x - state->quad_x);
		y1 = state->prev_y + (state->prev_y - state->quad_y);
		state->quad_x = x1;
		state->quad_y = y1;

		if (relative != 0) {
			x1 += state->prev_x;
			y1 += state->prev_y;
			state->cmd.p[cmdpc + 0] += state->prev_x; /* x */
			state->cmd.p[cmdpc + 1] += state->prev_y; /* y */
		}

		state->path.p[state->path.used++] = svgtiny_PATH_BEZIER;
		state->path.p[state->path.used++] =
			1./3 * state->prev_x +
			2./3 * x1;
		state->path.p[state->path.used++] =
			1./3 * state->prev_y +
			2./3 * y1;
		state->path.p[state->path.used++] =
			2./3 * x1 +
			1./3 * state->cmd.p[cmdpc + 0];
		state->path.p[state->path.used++] =
			2./3 * y1 +
			1./3 * state->cmd.p[cmdpc + 1];
		state->path.p[state->path.used++] =
			state->cubic_x =
			state->prev_x = state->cmd.p[cmdpc + 0];
		state->path.p[state->path.used++] =
			state->cubic_y =
			state->prev_y = state->cmd.p[cmdpc + 1];
	}
	return svgtiny_OK;
}


/**
 * rotate midpoint vector
 */
static void
rotate_midpoint_vector(float ax, float ay,
		       float bx, float by,
		       double radangle,
		       double *x_out, double *y_out)
{
	double dx2; /* midpoint x coordinate */
	double dy2; /* midpoint y coordinate */
	double cosangle; /* cosine of rotation angle */
	double sinangle; /* sine of rotation angle */

	/* compute the sin and cos of the angle */
	cosangle = cos(radangle);
	sinangle = sin(radangle);

	/* compute the midpoint between start and end points */
	dx2 = (ax - bx) / 2.0;
	dy2 = (ay - by) / 2.0;

	/* rotate vector to remove angle */
	*x_out = ((cosangle * dx2) + (sinangle * dy2));
	*y_out = ((-sinangle * dx2) + (cosangle * dy2));
}


/**
 * ensure the arc radii are large enough and scale as appropriate
 *
 * the radii need to be large enough if they are not they must be
 *  adjusted. This allows for elimination of differences between
 *  implementations especialy with rounding.
 */
static void
ensure_radii_scale(double x1_sq, double y1_sq,
		   float *rx, float *ry,
		   double *rx_sq, double *ry_sq)
{
	double radiisum;
	double radiiscale;

	/* set radii square values */
	(*rx_sq) = (*rx) * (*rx);
	(*ry_sq) = (*ry) * (*ry);

	radiisum = (x1_sq / (*rx_sq)) + (y1_sq / (*ry_sq));
	if (radiisum > 0.99999) {
		/* need to scale radii */
		radiiscale = sqrt(radiisum) * 1.00001;
		*rx = (float)(radiiscale * (*rx));
		*ry = (float)(radiiscale * (*ry));
		/* update squares too */
		(*rx_sq) = (*rx) * (*rx);
		(*ry_sq) = (*ry) * (*ry);
	}
}


/**
 * compute the transformed centre point
 */
static void
compute_transformed_centre_point(double sign, float rx, float ry,
				 double rx_sq, double ry_sq,
				 double x1, double y1,
				 double x1_sq, double y1_sq,
				 double *cx1, double *cy1)
{
	double sq;
	double coef;
	sq = ((rx_sq * ry_sq) - (rx_sq * y1_sq) - (ry_sq * x1_sq)) /
		((rx_sq * y1_sq) + (ry_sq * x1_sq));
	sq = (sq < 0) ? 0 : sq;

	coef = (sign * sqrt(sq));

	*cx1 = coef * ((rx * y1) / ry);
	*cy1 = coef * -((ry * x1) / rx);
}


/**
 * compute untransformed centre point
 *
 * \param ax The first point x coordinate
 * \param ay The first point y coordinate
 * \param bx The second point x coordinate
 * \param ay The second point y coordinate
 */
static void
compute_centre_point(float ax, float ay,
		     float bx, float by,
		     double cx1, double cy1,
		     double radangle,
		     double *x_out, double *y_out)
{
	double sx2;
	double sy2;
	double cosangle; /* cosine of rotation angle */
	double sinangle; /* sine of rotation angle */

	/* compute the sin and cos of the angle */
	cosangle = cos(radangle);
	sinangle = sin(radangle);

	sx2 = (ax + bx) / 2.0;
	sy2 = (ay + by) / 2.0;

	*x_out = sx2 + (cosangle * cx1 - sinangle * cy1);
	*y_out = sy2 + (sinangle * cx1 + cosangle * cy1);
}


/**
 * compute the angle start and extent
 */
static void
compute_angle_start_extent(float rx, float ry,
			   double x1, double y1,
			   double cx1, double cy1,
			   double *start, double *extent)
{
	double sign;
	double ux;
	double uy;
	double vx;
	double vy;
	double p, n;
	double actmp;

	/*
	 * Angle betwen two vectors is +/- acos( u.v / len(u) * len(v))
	 * Where:
	 *  '.' is the dot product.
	 *  +/- is calculated from the sign of the cross product (u x v)
	 */

	ux = (x1 - cx1) / rx;
	uy = (y1 - cy1) / ry;
	vx = (-x1 - cx1) / rx;
	vy = (-y1 - cy1) / ry;

	/* compute the start angle */
	/* The angle between (ux, uy) and the 0 angle */

	/* len(u) * len(1,0) == len(u) */
	n = sqrt((ux * ux) + (uy * uy));
	/* u.v == (ux,uy).(1,0) == (1 * ux) + (0 * uy) == ux */
	p = ux;
	/* u x v == (1 * uy - ux * 0) == uy */
	sign = (uy < 0) ? -1.0 : 1.0;
	/* (p >= n) so safe */
	*start = sign * acos(p / n);

	/* compute the extent angle */
	n = sqrt(((ux * ux) + (uy * uy)) * ((vx * vx) + (vy * vy)));
	p = (ux * vx) + (uy * vy);
	sign = ((ux * vy) - (uy * vx) < 0) ? -1.0f : 1.0f;

	/* arc cos must operate between -1 and 1 */
	actmp = p / n;
	if (actmp < -1.0) {
		*extent = sign * M_PI;
	} else if (actmp > 1.0) {
		*extent = 0;
	} else {
		*extent = sign * acos(actmp);
	}
}

/**
 * converts a circle centered unit circle arc to a series of bezier curves
 *
 * Each bezier is stored as six values of three pairs of coordinates
 *
 * The beziers are stored without their start point as that is assumed
 *   to be the preceding elements end point.
 *
 * \param start The start angle of the arc (in radians)
 * \param extent The size of the arc (in radians)
 * \param bzpt The array to store the bezier values in
 * \return The number of bezier segments output (max 4)
 */
static int
circle_arc_to_bezier(double start, double extent, double *bzpt)
{
	int bzsegments;
	double increment;
	double controllen;
	int pos = 0;
	int segment;
	double angle;
	double dx, dy;

	bzsegments = (int) ceil(fabs(extent) / M_PI_2);
	increment = extent / bzsegments;
	controllen = 4.0 / 3.0 * sin(increment / 2.0) / (1.0 + cos(increment / 2.0));

	for (segment = 0; segment < bzsegments; segment++) {
		/* first control point */
		angle = start + (segment * increment);
		dx = cos(angle);
		dy = sin(angle);
		bzpt[pos++] = dx - controllen * dy;
		bzpt[pos++] = dy + controllen * dx;
		/* second control point */
		angle+=increment;
		dx = cos(angle);
		dy = sin(angle);
		bzpt[pos++] = dx + controllen * dy;
		bzpt[pos++] = dy - controllen * dx;
		/* endpoint */
		bzpt[pos++] = dx;
		bzpt[pos++] = dy;

	}
	return bzsegments;
}


/**
 * transform coordinate list
 *
 * perform a scale, rotate and translate on list of coordinates
 *
 * scale(rx,ry)
 * rotate(an)
 * translate (cx, cy)
 *
 * homogeneous transforms
 *
 * scaling
 *     |   rx        0        0   |
 * S = |    0       ry        0   |
 *     |    0        0        1   |
 *
 * rotate
 *     | cos(an)  -sin(an)    0   |
 * R = | sin(an)   cos(an)    0   |
 *     |    0        0        1   |
 *
 * {{cos(a), -sin(a) 0}, {sin(a), cos(a),0}, {0,0,1}}
 *
 * translate
 *     |    1        0       cx   |
 * T = |    0        1       cy   |
 *     |    0        0        1   |
 *
 * note order is significat here and the combined matrix is
 * M = T.R.S
 *
 *       | cos(an)  -sin(an)    cx   |
 * T.R = | sin(an)   cos(an)    cy   |
 *       |    0        0        1    |
 *
 *         | rx * cos(an)  ry * -sin(an)   cx  |
 * T.R.S = | rx * sin(an)  ry * cos(an)    cy  |
 *         | 0             0               1   |
 *
 * {{Cos[a], -Sin[a], c}, {Sin[a], Cos[a], d}, {0, 0, 1}} . {{r, 0, 0}, {0, s, 0}, {0, 0, 1}}
 *
 * Each point
 *     | x1 |
 * P = | y1 |
 *     |  1 |
 *
 * output
 * | x2 |
 * | y2 | = M . P
 * | 1  |
 *
 * x2 = cx + (rx * x1 * cos(a)) + (ry * y1 * -1 * sin(a))
 * y2 = cy + (ry * y1 * cos(a)) + (rx * x1 * sin(a))
 *
 *
 * \param rx X scaling to apply
 * \param ry Y scaling to apply
 * \param radangle rotation to apply (in radians)
 * \param cx X translation to apply
 * \param cy Y translation to apply
 * \param points The size of the bzpoints array
 * \param bzpoints an array of x,y values to apply the transform to
 */
static void
scale_rotate_translate_points(double rx, double ry,
			      double radangle,
			      double cx, double cy,
			      int pntsize,
			      double *points)
{
	int pnt;
	double cosangle; /* cosine of rotation angle */
	double sinangle; /* sine of rotation angle */
	double rxcosangle, rxsinangle, rycosangle, rynsinangle;
	double x2,y2;

	/* compute the sin and cos of the angle */
	cosangle = cos(radangle);
	sinangle = sin(radangle);

	rxcosangle = rx * cosangle;
	rxsinangle = rx * sinangle;
	rycosangle = ry * cosangle;
	rynsinangle = ry * -1 * sinangle;

	for (pnt = 0; pnt < pntsize; pnt+=2) {
		x2 = cx + (points[pnt] * rxcosangle) + (points[pnt + 1] * rynsinangle);
		y2 = cy + (points[pnt + 1] * rycosangle) + (points[pnt] * rxsinangle);
		points[pnt] = x2;
		points[pnt + 1] = y2;
	}
}


/**
 * convert an svg path arc to a bezier curve
 *
 * This function perfoms a transform on the nine arc parameters
 *  (coordinate pairs for start and end together with the radii of the
 *  elipse, the rotation angle and which of the four arcs to draw)
 *  which generates the parameters (coordinate pairs for start,
 *  end and their control points) for a set of up to four bezier curves.
 *
 * Obviously the start and end coordinates are not altered between
 * representations so the aim is to calculate the coordinate pairs for
 * the bezier control points.
 *
 * \param bzpoints the array to fill with bezier curves
 * \return the number of bezier segments generated or -1 for a line
 */
static int
svgarc_to_bezier(float start_x,
		 float start_y,
		 float end_x,
		 float end_y,
		 float rx,
		 float ry,
		 float angle,
		 bool largearc,
		 bool sweep,
		 double *bzpoints)
{
	double radangle; /* normalised elipsis rotation angle in radians */
	double rx_sq; /* x radius squared */
	double ry_sq; /* y radius squared */
	double x1, y1; /* rotated midpoint vector */
	double x1_sq, y1_sq; /* x1 vector squared */
	double cx1,cy1; /* transformed circle center */
	double cx,cy; /* circle center */
	double start, extent;
	int bzsegments;

	if ((start_x == end_x) && (start_y == end_y)) {
		/*
		 * if the start and end coordinates are the same the
		 *  svg spec says this is equivalent to having no segment
		 *  at all
		 */
		return 0;
	}

	if ((rx == 0) || (ry == 0)) {
		/*
		 * if either radii is zero the specified behaviour is a line
		 */
		return -1;
	}

	/* obtain the absolute values of the radii */
	rx = fabsf(rx);
	ry = fabsf(ry);

	/* convert normalised angle to radians */
	radangle = degToRad(fmod(angle, 360.0));

	/* step 1 */
	/* x1,x2 is the midpoint vector rotated to remove the arc angle */
	rotate_midpoint_vector(start_x, start_y, end_x, end_y, radangle, &x1, &y1);

	/* step 2 */
	/* get squared x1 values */
	x1_sq = x1 * x1;
	y1_sq = y1 * y1;

	/* ensure radii are correctly scaled  */
	ensure_radii_scale(x1_sq, y1_sq, &rx, &ry, &rx_sq, &ry_sq);

	/* compute the transformed centre point */
	compute_transformed_centre_point(largearc == sweep?-1:1,
					 rx, ry,
					 rx_sq, ry_sq,
					 x1, y1,
					 x1_sq, y1_sq,
					 &cx1, &cy1);

	/* step 3 */
	/* get the untransformed centre point */
	compute_centre_point(start_x, start_y,
			     end_x, end_y,
			     cx1, cy1,
			     radangle,
			     &cx, &cy);

	/* step 4 */
	/* compute anglestart and extent */
	compute_angle_start_extent(rx,ry,
				   x1,y1,
				   cx1, cy1,
				   &start, &extent);

	/* extent of 0 is a straight line */
	if (extent == 0) {
		return -1;
	}

	/* take account of sweep */
	if (!sweep && extent > 0) {
		extent -= TAU;
	} else if (sweep && extent < 0) {
		extent += TAU;
	}

	/* normalise start and extent */
	extent = fmod(extent, TAU);
	start = fmod(start, TAU);

	/* convert the arc to unit circle bezier curves */
	bzsegments = circle_arc_to_bezier(start, extent, bzpoints);

	/* transform the bezier curves */
	scale_rotate_translate_points(rx, ry,
				      radangle,
				      cx, cy,
				      bzsegments * 6,
				      bzpoints);

	return bzsegments;
}

/**
 * elliptical arc
 *
 * command and parameters: A|a (rx, ry, rotation, largearc, sweep, x, y)+
 */
static svgtiny_code
generate_path_ellipticalarc(struct internal_path_state *state, int relative)
{
	svgtiny_code res;
	unsigned int cmdpc = 0; /* command parameter count */

	if ((state->cmd.used < 7) || ((state->cmd.used % 7) != 0)) {
		/* wrong number of parameters */
		return svgtiny_SVG_ERROR;
	}

	for (cmdpc = 0; cmdpc < state->cmd.used; cmdpc += 7) {
		int bzsegments;
		double bzpoints[6*4]; /* allow for up to four bezier segments per arc */
		if (relative != 0) {
			state->cmd.p[cmdpc + 5] += state->prev_x; /* x */
			state->cmd.p[cmdpc + 6] += state->prev_y; /* y */
		}

		bzsegments = svgarc_to_bezier(state->prev_x, state->prev_y,
					      state->cmd.p[cmdpc + 5], /* x */
					      state->cmd.p[cmdpc + 6], /* y */
					      state->cmd.p[cmdpc + 0], /* rx */
					      state->cmd.p[cmdpc + 1], /* ry */
					      state->cmd.p[cmdpc + 2], /* rotation */
					      state->cmd.p[cmdpc + 3], /* large_arc */
					      state->cmd.p[cmdpc + 4], /* sweep */
					      bzpoints);

		if (bzsegments == -1) {
			/* failed to convert arc to bezier replace with line */
			res = ensure_internal_points(&state->path, 3);
			if (res != svgtiny_OK) {
				return res;
			}
			state->path.p[state->path.used++] = svgtiny_PATH_LINE;
			state->path.p[state->path.used++] =
				state->cubic_x =
				state->quad_x =
				state->prev_x = state->cmd.p[cmdpc + 5] /* x */;
			state->path.p[state->path.used++] =
				state->cubic_y =
				state->quad_y =
				state->prev_y = state->cmd.p[cmdpc + 6] /* y */;
		} else if (bzsegments > 0){
			int bzpnt;
			for (bzpnt = 0;bzpnt < (bzsegments * 6); bzpnt+=6) {
				res = ensure_internal_points(&state->path, 7);
				if (res != svgtiny_OK) {
					return res;
				}
				state->path.p[state->path.used++] = svgtiny_PATH_BEZIER;
				state->path.p[state->path.used++] = bzpoints[bzpnt];
				state->path.p[state->path.used++] = bzpoints[bzpnt+1];
				state->path.p[state->path.used++] = bzpoints[bzpnt+2];
				state->path.p[state->path.used++] = bzpoints[bzpnt+3];
				state->path.p[state->path.used++] =
					state->cubic_y =
					state->quad_x =
					state->prev_x = bzpoints[bzpnt+4];
				state->path.p[state->path.used++] =
					state->cubic_y =
					state->quad_y =
					state->prev_y = bzpoints[bzpnt+5];
			}
		}
	}
	return svgtiny_OK;
}


/**
 * parse parameters to a path command
 */
static inline svgtiny_code
parse_path_parameters(const char **cursor,
		      const char *textend,
		      struct internal_points *cmdp)
{
	const char *numend;
	svgtiny_code res;

	cmdp->used = 0;

	do {
		res = ensure_internal_points(cmdp, 1);
		if (res != svgtiny_OK) {
			return res;
		}

		numend = textend;
		res = svgtiny_parse_number(*cursor,
					   &numend,
					   cmdp->p + cmdp->used);
		if (res != svgtiny_OK) {
			break;
		}
		cmdp->used++;
		*cursor = numend;

		advance_comma_whitespace(cursor, textend);

	} while (res == svgtiny_OK);

	return svgtiny_OK;
}


/**
 * parse a path command
 */
static inline svgtiny_code
parse_path_command(const char **cursor,
		   const char *textend,
		   char *pathcommand,
		   int *relative)
{
	advance_whitespace(cursor, textend);

	if ((**cursor != 'M') && (**cursor != 'm') &&
	    (**cursor != 'Z') && (**cursor != 'z') &&
	    (**cursor != 'L') && (**cursor != 'l') &&
	    (**cursor != 'H') && (**cursor != 'h') &&
	    (**cursor != 'V') && (**cursor != 'v') &&
	    (**cursor != 'C') && (**cursor != 'c') &&
	    (**cursor != 'S') && (**cursor != 's') &&
	    (**cursor != 'Q') && (**cursor != 'q') &&
	    (**cursor != 'T') && (**cursor != 't') &&
	    (**cursor != 'A') && (**cursor != 'a')) {
		return svgtiny_SVG_ERROR;
	}

	if ((**cursor >= 0x61 /* a */) && (**cursor <= 0x7A /* z */)) {
		*pathcommand = (**cursor) & ~(1 << 5); /* upper case char */
		*relative = 1;
	} else {
		*pathcommand = **cursor;
		*relative = 0;
	}
	(*cursor)++;

	advance_whitespace(cursor, textend);

	return svgtiny_OK;
}


/**
 * parse path data attribute into a shape list
 */
svgtiny_code
svgtiny_parse_path_data(const char *text,
			size_t textlen,
			float **pointv,
			unsigned int *pointc)
{
	const char *cursor = text; /* text cursor */
	const char *textend = text + textlen;
	svgtiny_code res;
	char pathcmd = 0;
	int relative = 0;
	struct internal_path_state pathstate = {
		{NULL, 0, 0},
		{NULL, 0, 0},
		0.0, 0.0,
		0.0, 0.0,
		0.0, 0.0,
		0.0, 0.0
	};

	advance_whitespace(&cursor, textend);

	/* empty path data - equivalent to none */
	if (cursor == textend) {
		*pointc = 0;
		return svgtiny_OK;
	}

	/* none */
	res = svgtiny_parse_none(cursor, textend);
	if (res == svgtiny_OK) {
		*pointc = 0;
		return res;
	}

	while (cursor < textend) {
		res = parse_path_command(&cursor, textend, &pathcmd, &relative);
		if (res != svgtiny_OK) {
			goto parse_path_data_error;
		}

		res = parse_path_parameters(&cursor, textend, &pathstate.cmd);
		if (res != svgtiny_OK) {
			goto parse_path_data_error;
		}

		switch (pathcmd) {
		case 'M':
			res = generate_path_move(&pathstate, relative);
			break;
		case 'Z':
			res = generate_path_close(&pathstate);
			break;
		case 'L':
			res = generate_path_line(&pathstate, relative);
			break;
		case 'H':
			res = generate_path_hline(&pathstate, relative);
			break;
		case 'V':
			res = generate_path_vline(&pathstate, relative);
			break;
		case 'C':
			res = generate_path_curveto(&pathstate, relative);
			break;
		case 'S':
			res = generate_path_scurveto(&pathstate, relative);
			break;
		case 'Q':
			res = generate_path_bcurveto(&pathstate, relative);
			break;
		case 'T':
			res = generate_path_sbcurveto(&pathstate, relative);
			break;
		case 'A':
			res = generate_path_ellipticalarc(&pathstate, relative);
			break;
		}

		if (res != svgtiny_OK) {
			goto parse_path_data_error;
		}

	}
	*pointv = pathstate.path.p;
	*pointc = pathstate.path.used;

	if (pathstate.cmd.alloc > 0) {
		free(pathstate.cmd.p);
	}
	return svgtiny_OK;

parse_path_data_error:
	/* free any local state */
	if (pathstate.path.alloc > 0) {
		free(pathstate.path.p);
	}
	if (pathstate.cmd.alloc > 0) {
		free(pathstate.cmd.p);
	}
	return res;
}
