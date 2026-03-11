#include <stdio.h>
#include <stdint.h>
#include <math.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

// Convert polar LCH / OKLCH to cartesian LAB / OKLAB
void polar_to_cartesian(float l, float c, float h_deg, float *out_l, float *out_a, float *out_b) {
    *out_l = l;
    *out_a = c * cos(h_deg * M_PI / 180.0);
    *out_b = c * sin(h_deg * M_PI / 180.0);
}

// Convert linear sRGB channel to gamma-corrected sRGB channel (0-255)
uint8_t linear_srgb_to_srgb8(float linear) {
    float srgb;
    if (linear <= 0.0) srgb = 0.0;
    else if (linear >= 1.0) srgb = 1.0;
    else if (linear <= 0.0031308) srgb = 12.92 * linear;
    else srgb = 1.055 * pow(linear, 1.0 / 2.4) - 0.055;
    
    int val = (int)(srgb * 255.0 + 0.5);
    if (val < 0) return 0;
    if (val > 255) return 255;
    return val;
}

// OKLAB to linear sRGB
void oklab_to_linear_srgb(float L, float a, float b, float *r, float *g, float *b_out) {
    float l_ = L + 0.3963377774f * a + 0.2158037573f * b;
    float m_ = L - 0.1055613458f * a - 0.0638541728f * b;
    float s_ = L - 0.0894841775f * a - 1.2914855480f * b;

    l_ = l_ * l_ * l_;
    m_ = m_ * m_ * m_;
    s_ = s_ * s_ * s_;

    *r = +4.0767416621f * l_ - 3.3077115913f * m_ + 0.2309699292f * s_;
    *g = -1.2684380046f * l_ + 2.6097574011f * m_ - 0.3413193965f * s_;
    *b_out = -0.0041960863f * l_ - 0.7034186147f * m_ + 1.7076147010f * s_;
}

int main() {
    float r, g, b;
    // Test OKLAB to sRGB (e.g. white)
    oklab_to_linear_srgb(1.0, 0.0, 0.0, &r, &g, &b);
    printf("White: rgba(%d, %d, %d)\n", linear_srgb_to_srgb8(r), linear_srgb_to_srgb8(g), linear_srgb_to_srgb8(b));
    
    // Tailwind blue-500: oklch(0.623 0.214 259.815)
    float L, A, B;
    polar_to_cartesian(0.623, 0.214, 259.815, &L, &A, &B);
    oklab_to_linear_srgb(L, A, B, &r, &g, &b);
    
    // Convert to sRGB 8-bit
    uint8_t r8 = linear_srgb_to_srgb8(r);
    uint8_t g8 = linear_srgb_to_srgb8(g);
    uint8_t b8 = linear_srgb_to_srgb8(b);
    
    printf("Tailwind Blue-500: rgb(%d, %d, %d) hex: #%02x%02x%02x\n", r8, g8, b8, r8, g8, b8);
    // Should be near #3b82f6
    return 0;
}
