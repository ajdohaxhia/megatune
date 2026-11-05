use crate::parameters::{DEFAULT_SPEED, DEFAULT_SUSTAIN, DEFAULT_AMOUNT, DEFAULT_DOUBLER, DEFAULT_WIDTH};

/// Factory programs shared with the VST3 build. Columns are Speed (ms),
/// Sustain, Amount, Doubler and Stereo Width.
pub const PRESETS: &[(&str,f32,f32,f32,f32,f32)] = &[
("Default",DEFAULT_SPEED,DEFAULT_SUSTAIN,DEFAULT_AMOUNT,DEFAULT_DOUBLER,DEFAULT_WIDTH),
("Natural Tune",90.,-20.,70.,0.,0.),
("Modern Pop",45.,-10.,95.,8.,12.),
("Polished Pop",70.,-25.,85.,10.,20.),
("R&B Smooth",120.,-40.,75.,15.,25.),
("Hip-Hop Lead",25.,10.,100.,5.,10.),
("Doubler Wide",25.,0.,100.,65.,100.),
("Trap Robotic",-3.,40.,100.,0.,0.),
("EDM Vocal",15.,20.,100.,20.,45.),
("Rock Anthem",60.,-15.,90.,12.,30.),
("Indie Folk",150.,-45.,60.,0.,0.),
("Lo-Fi Bed",200.,-60.,55.,35.,60.),
("Doubler Tight",30.,0.,100.,35.,15.),
("Subtle Assist",180.,-30.,40.,0.,0.),
("Extreme Tune",-3.,60.,100.,0.,0.),
("Spoken Word",100.,-10.,50.,0.,0.)];

#[cfg(test)]
mod tests {
    #[test]
    fn default_preset_matches_recovered_controls() {
        assert_eq!(super::PRESETS[0], ("Default",25.,0.,100.,0.,0.));
    }

    #[test]
    fn industry_preset_set_is_complete() {
        assert_eq!(super::PRESETS.len(), 16);
        assert!(super::PRESETS.iter().any(|p| p.0 == "Trap Robotic" && p.1 < 0.));
        assert!(super::PRESETS.iter().any(|p| p.0 == "Doubler Wide" && p.4 > 60.));
        for preset in super::PRESETS {
            assert!(!preset.0.is_empty());
            assert!(preset.1 >= -3. && preset.1 <= 200.);
            assert!(preset.2 >= -100. && preset.2 <= 100.);
            assert!(preset.3 >= 0. && preset.3 <= 100.);
            assert!(preset.4 >= 0. && preset.4 <= 100.);
            assert!(preset.5 >= 0. && preset.5 <= 100.);
        }
    }
}
