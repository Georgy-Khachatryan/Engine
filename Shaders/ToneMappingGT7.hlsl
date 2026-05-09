#ifndef TONEMAPPINGGT7_HLSL
#define TONEMAPPINGGT7_HLSL

//
// Driving Toward Reality: Physically Based Tone Mapping and Perceptual Fidelity in Gran Turismo 7
// See license in THIRD_PARTY_LICENSES.md
//

// Unified color space options:
#define TONE_MAPPING_UCS_ICTCP  0
#define TONE_MAPPING_UCS_JZAZBZ 1
#define TONE_MAPPING_UCS        TONE_MAPPING_UCS_ICTCP

// Converts linear frame buffer value to physical luminance (cd/m^2)
// where 1.0 corresponds to reference_luminance (e.g., 80 cd/m^2).
float FrameBufferValueToPhysicalValue(float frame_buffer_value) { return frame_buffer_value * ToneMappingGpuConstants::reference_luminance; }

// Converts physical luminance (cd/m^2) to a linear frame buffer value,
// where 1.0 corresponds to reference_luminance (e.g., 80 cd/m^2).
float PhysicalValueToFrameBufferValue(float physical_value) { return physical_value / ToneMappingGpuConstants::reference_luminance; }


// Base functions from SMPTE ST 2084:2014
// Converts from normalized PQ (0-1) to absolute luminance in cd/m^2 (linear light)
// Assumes float input; does not handle integer encoding (Annex)
// Assumes full-range signal (0-1)
static const float pq_m1 = 0.1593017578125; // (2610 / 4096) / 4
static const float pq_m2 = 78.84375;        // (2523 / 4096) * 128
static const float pq_c1 = 0.8359375;       // (3424 / 4096)
static const float pq_c2 = 18.8515625;      // (2413 / 4096) * 32
static const float pq_c3 = 18.6875;         // (2392 / 4096) * 32
static const float pq_c  = 10000.0;         // Maximum luminance supported by PQ (cd/m^2)

// EOTF / inverse-EOTF for ST-2084 (PQ).
// Note: Introduce exponent_scale_factor to allow scaling of the exponent in the EOTF for Jzazbz.
float EotfSt2084(float n, float exponent_scale_factor = 1.0) {
	n = saturate(n);
	
	// Does not handle signal range from 2084 - assumes full range (0-1)
	float np = pow(n, 1.0 / (pq_m2 * exponent_scale_factor));
	float l  = np - pq_c1;
	
	if (l < 0.0) l = 0.0;
	
	l = l / (pq_c2 - pq_c3 * np);
	l = pow(l, 1.0 / pq_m1);
	
	// Convert absolute luminance (cd/m^2) into the frame buffer linear scale.
	return PhysicalValueToFrameBufferValue(l * pq_c);
}

float InverseEotfSt2084(float v, float exponent_scale_factor = 1.0) {
	// Convert the frame buffer linear scale into absolute luminance (cd/m^2).
	float physical = FrameBufferValueToPhysicalValue(v);
	float y        = physical / pq_c; // Normalize for the ST-2084 curve
	
	float ym = pow(y, pq_m1);
	return exp2(pq_m2 * exponent_scale_factor * (log2(pq_c1 + pq_c2 * ym) - log2(1.0 + pq_c3 * ym)));
}


// ICtCp conversion.
// Reference: ITU-T T.302 (https://www.itu.int/rec/T-REC-T.302/en)
#if (TONE_MAPPING_UCS == TONE_MAPPING_UCS_ICTCP)

// Input: Linear Rec.2020
float3 RgbToIctcp(float3 rgb) {
	float l = (rgb[0] * 1688.0 + rgb[1] * 2146.0 + rgb[2] * 262.0)  / 4096.0;
	float m = (rgb[0] * 683.0  + rgb[1] * 2951.0 + rgb[2] * 462.0)  / 4096.0;
	float s = (rgb[0] * 99.0   + rgb[1] * 309.0  + rgb[2] * 3688.0) / 4096.0;
	
	float l_pq = InverseEotfSt2084(l);
	float m_pq = InverseEotfSt2084(m);
	float s_pq = InverseEotfSt2084(s);
	
	float3 ictcp;
	ictcp[0] = (2048.0  * l_pq + 2048.0  * m_pq) / 4096.0;
	ictcp[1] = (6610.0  * l_pq - 13613.0 * m_pq + 7003.0 * s_pq) / 4096.0;
	ictcp[2] = (17933.0 * l_pq - 17390.0 * m_pq - 543.0  * s_pq) / 4096.0;
	return ictcp;
}

// Output: Linear Rec.2020
float3 IctcpToRgb(float3 ictcp) {
	float l = ictcp[0] + 0.00860904 * ictcp[1] + 0.11103  * ictcp[2];
	float m = ictcp[0] - 0.00860904 * ictcp[1] - 0.11103  * ictcp[2];
	float s = ictcp[0] + 0.560031   * ictcp[1] - 0.320627 * ictcp[2];
	
	float l_lin = EotfSt2084(l);
	float m_lin = EotfSt2084(m);
	float s_lin = EotfSt2084(s);
	
	float3 rgb;
	rgb[0] = max(3.43661    * l_lin - 2.50645   * m_lin + 0.0698454 * s_lin, 0.0);
	rgb[1] = max(-0.79133   * l_lin + 1.9836    * m_lin - 0.192271  * s_lin, 0.0);
	rgb[2] = max(-0.0259499 * l_lin - 0.0989137 * m_lin + 1.12486   * s_lin, 0.0);
	return rgb;
}
#endif // (TONE_MAPPING_UCS == TONE_MAPPING_UCS_ICTCP)


// Jzazbz conversion.
// Reference:
// Muhammad Safdar, Guihua Cui, Youn Jin Kim, and Ming Ronnier Luo,
// "Perceptually uniform color space for image signals including high dynamic
// range and wide gamut," Opt. Express 25, 15131-15151 (2017)
// Note: Coefficients adjusted for linear Rec.2020
#if (TONE_MAPPING_UCS == TONE_MAPPING_UCS_JZAZBZ)
static const float jzazbz_exponent_scale_factor = 1.7;

// Input: linear Rec.2020
float3 RgbToJzazbz(float3 rgb) {
	float l = rgb[0] * 0.530004 + rgb[1] * 0.355704 + rgb[2] * 0.086090;
	float m = rgb[0] * 0.289388 + rgb[1] * 0.525395 + rgb[2] * 0.157481;
	float s = rgb[0] * 0.091098 + rgb[1] * 0.147588 + rgb[2] * 0.734234;
	
	float l_pq = InverseEotfSt2084(l, jzazbz_exponent_scale_factor);
	float m_pq = InverseEotfSt2084(m, jzazbz_exponent_scale_factor);
	float s_pq = InverseEotfSt2084(s, jzazbz_exponent_scale_factor);
	
	float iz = 0.5 * l_pq + 0.5 * m_pq;
	
	float3 jab;
	jab[0] = (0.44 * iz) / (1.0 - 0.56 * iz) - 1.6295499532821566e-11;
	jab[1] = 3.524000 * l_pq - 4.066708 * m_pq + 0.542708 * s_pq;
	jab[2] = 0.199076 * l_pq + 1.096799 * m_pq - 1.295875 * s_pq;
	return jab;
}

// Output: linear Rec.2020
float3 JzazbzToRgb(float3 jab) {
	float jz = jab[0] + 1.6295499532821566e-11;
	float iz = jz / (0.44 + 0.56 * jz);
	float a  = jab[1];
	float b  = jab[2];
	
	float l = iz + a * +1.386050432715393e-1 + b * +5.804731615611869e-2;
	float m = iz + a * -1.386050432715393e-1 + b * -5.804731615611869e-2;
	float s = iz + a * -9.601924202631895e-2 + b * -8.118918960560390e-1;
	
	float l_lin = EotfSt2084(l, jzazbz_exponent_scale_factor);
	float m_lin = EotfSt2084(m, jzazbz_exponent_scale_factor);
	float s_lin = EotfSt2084(s, jzazbz_exponent_scale_factor);
	
	float3 rgb;
	rgb[0] = l_lin * +2.990669 + m_lin * -2.049742 + s_lin * +0.088977;
	rgb[1] = l_lin * -1.634525 + m_lin * +3.145627 + s_lin * -0.483037;
	rgb[2] = l_lin * -0.042505 + m_lin * -0.377983 + s_lin * +1.448019;
	return rgb;
}
#endif // (TONE_MAPPING_UCS == TONE_MAPPING_UCS_JZAZBZ)


// Unified color space (UCS): ICtCp or Jzazbz.
#if (TONE_MAPPING_UCS == TONE_MAPPING_UCS_ICTCP)
float3 RgbToUcs(float3 rgb) { return RgbToIctcp(rgb); }
float3 UcsToRgb(float3 ucs) { return IctcpToRgb(ucs); }
#elif (TONE_MAPPING_UCS == TONE_MAPPING_UCS_JZAZBZ)
float3 RgbToUcs(float3 rgb) { return RgbToJzazbz(rgb); }
float3 UcsToRgb(float3 ucs) { return JzazbzToRgb(ucs); }
#else // (TONE_MAPPING_UCS != TONE_MAPPING_UCS_JZAZBZ)
#error "Unsupported TONE_MAPPING_UCS value. Please define TONE_MAPPING_UCS as either TONE_MAPPING_UCS_ICTCP or TONE_MAPPING_UCS_JZAZBZ."
#endif // (TONE_MAPPING_UCS != TONE_MAPPING_UCS_JZAZBZ)


// "GT Tone Mapping" curve with convergent shoulder.
float EvaluateToneMappingCurveGT7(ToneMappingGpuConstants constants, float x) {
	if (x < 0.0) return 0.0;
	
	if (x < constants.toe_threshold) {
		float weight_linear = smoothstep(0.0, constants.mid_point, x);
		float weight_toe = 1.0 - weight_linear;
		float toe_mapped = constants.mid_point * pow(x / constants.mid_point, constants.toe_power);
		
		return weight_toe * toe_mapped + weight_linear * x;
	} else {
		// Shoulder mapping for highlights.
		return constants.k_a + constants.k_b * exp(x * constants.k_c);
	}
}

float EvaluateChromaCurveGT7(float x, float a, float b) {
	return smoothstep(b, a, x);
}

// Input:  Linear Rec.2020 RGB (frame buffer values)
// Output: Tone-mapped RGB (frame buffer values);
//         - In SDR mode: mapped to [0, 1], ready for sRGB OETF
//         - In HDR mode: mapped to [0, framebuffer_luminance_target], ready for PQ inverse-EOTF
// Note: framebuffer_luminance_target represents the display's target peak luminance converted to a frame buffer value.
//       The returned values are suitable for applying the appropriate OETF to generate final output signal.
float3 ApplyToneMappingGT7(ToneMappingGpuConstants constants, float3 rgb) {
	// Convert RGB to UCS to separate luminance and chroma.
	float3 ucs = RgbToUcs(rgb);
	
	// Per-channel tone mapping ("skewed" color).
	float3 skewed_rgb;
	skewed_rgb.x = EvaluateToneMappingCurveGT7(constants, rgb.x);
	skewed_rgb.y = EvaluateToneMappingCurveGT7(constants, rgb.y);
	skewed_rgb.z = EvaluateToneMappingCurveGT7(constants, rgb.z);
	
	float3 skewed_ucs = RgbToUcs(skewed_rgb);
	
	float framebuffer_luminance_target_ucs = RgbToUcs(constants.framebuffer_luminance_target).x; // Target luminance in UCS space.
	float chroma_scale = EvaluateChromaCurveGT7(ucs.x / framebuffer_luminance_target_ucs, constants.fade_start, constants.fade_end);
	
	float3 scaled_ucs;
	scaled_ucs.x  = skewed_ucs.x;          // Luminance from skewed color.
	scaled_ucs.yz = ucs.yz * chroma_scale; // Scaled chroma components.
	
	// Convert UCS back to RGB.
	float3 scaled_rgb = UcsToRgb(scaled_ucs);
	
	// Final blend between per-channel and UCS-scaled results.
	float3 blended_rgb = lerp(skewed_rgb, scaled_rgb, constants.blend_ratio);
	
	// When using SDR, apply the correction factor.
	// When using HDR, sdr_correction_factor is 1.0, so it has no effect.
	return constants.sdr_correction_factor * min(blended_rgb, constants.framebuffer_luminance_target);
}

#endif // TONEMAPPINGGT7_HLSL
