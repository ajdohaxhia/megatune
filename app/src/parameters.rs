//! Original control mappings, separate from the independently estimated DSP.
//! ARM64 functions: 0xf9a798, 0xf9a7f8, 0xf9a84c, 0x13f3588, 0x13f3658.

// Split weights recovered at 0xf9a84c (IEEE-754 doubles).
const SPEED_SPLIT: f64 = 0.784;
const SPEED_NEGATIVE_WEIGHT: f64 = 0.21599999999999997;

// Registration blocks 0xfa1758, 0xfa1940, 0xfa1d24, 0xfa2228, 0xfa2408.
pub const DEFAULT_SPEED: f32 = 25.0;
pub const DEFAULT_AMOUNT: f32 = 100.0;
pub const DEFAULT_SUSTAIN: f32 = 0.0;
pub const DEFAULT_DOUBLER: f32 = 0.0;
pub const DEFAULT_WIDTH: f32 = 0.0;

pub fn speed_from_normalized(normalized: f32) -> f32 {
    let normalized = normalized.clamp(0.0, 1.0) as f64;
    if normalized < SPEED_SPLIT {
        ((30625.0 * 49_f64.powf(-normalized / SPEED_SPLIT) - 625.0) / 150.0) as f32
    } else {
        (-3.0 * (normalized - SPEED_SPLIT) / SPEED_NEGATIVE_WEIGHT) as f32
    }
}

pub fn speed_to_normalized(speed: f32) -> f32 {
    let speed = speed.clamp(-3.0, 200.0) as f64;
    if speed > 0.0 {
        (SPEED_SPLIT * ((150.0 * speed + 625.0) / 30625.0).ln() / (1_f64 / 49.0).ln()) as f32
    } else {
        (SPEED_SPLIT - SPEED_NEGATIVE_WEIGHT * speed / 3.0) as f32
    }
}

pub fn amount_from_normalized(normalized: f32) -> f32 {
    (112.5 * (1.0 - 9_f64.powf(-(normalized.clamp(0.0, 1.0) as f64)))) as f32
}

pub fn amount_to_normalized(amount: f32) -> f32 {
    ((1.0 - amount.clamp(0.0, 100.0) as f64 / 112.5).ln() / (1_f64 / 9.0).ln()) as f32
}

// Linear constructor 0x13f3808, Sustain registration block 0xfa1940.
pub fn sustain_from_normalized(normalized: f32) -> f32 {
    normalized.clamp(0.0, 1.0) * 200.0 - 100.0
}

pub fn sustain_to_normalized(sustain: f32) -> f32 {
    (sustain.clamp(-100.0, 100.0) + 100.0) / 200.0
}

#[cfg(test)]
mod tests {
    use super::*;

    fn close(actual: f32, expected: f32) {
        assert!((actual - expected).abs() < 0.0001, "{actual} != {expected}");
    }

    #[test]
    fn sustain_matches_original_linear_range() {
        for (n, value) in [(0.0, -100.0), (0.5, 0.0), (1.0, 100.0)] {
            close(sustain_from_normalized(n), value);
            close(sustain_to_normalized(value), n);
        }
        close(sustain_from_normalized(-1.0), -100.0);
        close(sustain_from_normalized(2.0), 100.0);
        close(sustain_to_normalized(-200.0), 0.0);
        close(sustain_to_normalized(200.0), 1.0);
    }

    #[test]
    fn speed_matches_original_landmarks() {
        for (normalized, speed) in [(0.0, 200.0), (0.392, 25.0), (0.784, 0.0), (0.892, -1.5), (1.0, -3.0)] {
            close(speed_from_normalized(normalized), speed);
            close(speed_to_normalized(speed), normalized);
        }
    }

    #[test]
    fn amount_matches_original_landmarks() {
        for (normalized, amount) in [(0.0, 0.0), (0.5, 75.0), (1.0, 100.0)] {
            close(amount_from_normalized(normalized), amount);
            close(amount_to_normalized(amount), normalized);
        }
    }

    #[test]
    fn mappings_roundtrip_and_clamp_boundaries() {
        for step in 0..=100 {
            let normalized = step as f32 / 100.0;
            close(speed_to_normalized(speed_from_normalized(normalized)), normalized);
            close(amount_to_normalized(amount_from_normalized(normalized)), normalized);
        }
        close(speed_from_normalized(-1.0), 200.0);
        close(speed_from_normalized(2.0), -3.0);
        close(speed_to_normalized(250.0), 0.0);
        close(speed_to_normalized(-10.0), 1.0);
        close(amount_from_normalized(-1.0), 0.0);
        close(amount_from_normalized(2.0), 100.0);
        close(amount_to_normalized(-1.0), 0.0);
        close(amount_to_normalized(101.0), 1.0);
    }
}
