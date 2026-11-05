//! Independent monophonic tuning engine. Positive sustain speeds up held-note
//! correction, as verified by the original's messages; timing formulas remain estimates.
#[cfg(test)]
mod tests {
    use super::*;
    fn tone(hz: f32) -> Audio {
        Audio { samples: (0..24000).map(|i| (i as f32 * hz * std::f32::consts::TAU / 24000.0).sin() * 0.5).collect(), rate: 24000, channels: 1 }
    }
    #[test]
    fn pitch_detection_tracks_a4() {
        let a = tone(440.0);
        assert!((detect_pitch(&a.samples[..2048], a.rate).unwrap() - 440.0).abs() < 3.0);
    }
    #[test]
    fn silence_is_unvoiced() { assert!(detect_pitch(&[0.0;2048], 24000).is_none()); }
    #[test]
    fn sustain_speeds_up_held_notes_only_when_positive() {
        let positive = smoothing_seconds(25.0, 100.0, 1.0);
        let neutral = smoothing_seconds(25.0, 0.0, 1.0);
        let negative = smoothing_seconds(25.0, -100.0, 1.0);
        assert!(positive < neutral && neutral < negative);
        for sustain in [-100.0, 0.0, 100.0] {
            assert_eq!(smoothing_seconds(25.0, sustain, 0.0), neutral);
        }
    }
    #[test]
    fn scale_selection_crosses_octaves() {
        let mut notes=[false;12]; notes[0]=true;
        assert_eq!(nearest_note(71.8,&notes),Some(72.0));
        assert_eq!(nearest_note(59.6,&notes),Some(60.0));
        assert_eq!(nearest_note(60.0,&[false;12]),None);
    }
    #[test]
    fn bypass_and_zero_amount_are_exact() {
        let a = tone(432.0);
        let mut s = Settings::default();
        s.bypass = true;
        assert_eq!(process(&a,&s).samples, a.samples);
        s.bypass = false; s.amount = 0.0;
        assert_eq!(process(&a,&s).samples, a.samples);
    }
    #[test]
    fn all_disabled_is_safe() {
        let a = tone(432.0);
        let mut s = Settings::default(); s.notes = [false;12];
        assert_eq!(process(&a,&s).samples, a.samples);
    }
    #[test]
    fn correction_preserves_duration_and_moves_pitch_up() {
        let a = tone(432.0);
        let mut s = Settings::default(); s.speed = 0.0;
        let b = process(&a,&s);
        assert_eq!(a.samples.len(),b.samples.len());
        assert!(b.samples.iter().all(|x|x.is_finite()));
        let hz = detect_pitch(&b.samples[12000..14048],b.rate).unwrap();
        assert!((hz-440.0).abs() < 8.0,"{hz}");
    }
    #[test]
    fn transpose_and_doubler_change_the_output() {
        let a=tone(440.0);
        let dry=Settings {amount:0.0,..Settings::default()};
        let shifted=process(&a,&Settings {shift:7.0,..dry.clone()});
        let doubled=process(&a,&Settings {doubler:100.0,..dry});
        assert_ne!(shifted.samples,a.samples);
        assert_ne!(doubled.samples,a.samples);
        assert_eq!(shifted.samples.len(),a.samples.len());
        assert!(doubled.samples.iter().all(|s|s.is_finite()));
    }
    #[test]
    fn wav_roundtrip_preserves_metadata() {
        let a = tone(440.0);
        let path = std::env::temp_dir().join(format!("tuning-audio-test-{}.wav",std::process::id()));
        save(&path,&a).unwrap();
        let b = load(&path).unwrap();
        let _ = std::fs::remove_file(path);
        assert_eq!((b.rate,b.channels,b.samples.len()),(a.rate,a.channels,a.samples.len()));
    }
}

use std::path::Path;

#[derive(Clone)]
pub struct Audio { pub samples: Vec<f32>, pub rate: u32, pub channels: u16 }

#[derive(Clone, serde::Serialize, serde::Deserialize)]
pub struct Settings {
    pub speed: f32,
    pub sustain: f32,
    pub amount: f32,
    pub notes: [bool;12],
    pub shift: f32,
    pub doubler: f32,
    pub width: f32,
    pub bypass: bool,
    pub stabilize: bool,
}
impl Default for Settings {
    fn default() -> Self {
        Self { speed: 25.0, sustain: 0.0, amount: 100.0, notes:[true;12], shift:0.0,
            doubler:0.0, width:0.0, bypass:false, stabilize:false }
    }
}

pub fn load(path: &Path) -> Result<Audio,String> {
    let meta = std::fs::metadata(path).map_err(|e|e.to_string())?;
    if meta.len() > 512*1024*1024 { return Err("Please choose a WAV smaller than 512 MB.".into()); }
    let mut reader = hound::WavReader::open(path).map_err(|e|format!("Cannot read WAV: {e}"))?;
    let spec = reader.spec();
    if !(1..=2).contains(&spec.channels) || !(8000..=192000).contains(&spec.sample_rate) {
        return Err("Choose a mono or stereo WAV from 8 to 192 kHz.".into());
    }
    let samples: Result<Vec<f32>,_> = match spec.sample_format {
        hound::SampleFormat::Float => reader.samples::<f32>().collect(),
        hound::SampleFormat::Int => {
            let divisor = 2_f32.powi(spec.bits_per_sample as i32 - 1);
            reader.samples::<i32>().map(|x|x.map(|n| n as f32/divisor)).collect()
        }
    };
    let samples = samples.map_err(|e|format!("Invalid WAV data: {e}"))?;
    if samples.is_empty() || samples.len()%spec.channels as usize != 0 || samples.iter().any(|x|!x.is_finite()) {
        return Err("The WAV has empty or invalid audio samples.".into());
    }
    Ok(Audio { samples, rate:spec.sample_rate, channels:spec.channels })
}

pub fn save(path: &Path, audio: &Audio) -> Result<(),String> {
    let spec = hound::WavSpec { channels:audio.channels, sample_rate:audio.rate,
        bits_per_sample:24,sample_format:hound::SampleFormat::Int };
    let mut writer = hound::WavWriter::create(path,spec).map_err(|e|e.to_string())?;
    for sample in &audio.samples {
        writer.write_sample((sample.clamp(-1.0,1.0)*8_388_607.0) as i32).map_err(|e|e.to_string())?;
    }
    writer.finalize().map_err(|e|e.to_string())
}

/// Normalized autocorrelation, with parabolic peak interpolation.
pub fn detect_pitch(input: &[f32], rate:u32) -> Option<f32> {
    if input.len()<128 || rate==0 { return None; }
    let step = (rate/12000).max(1) as usize;
    let samples:Vec<f32> = input.iter().step_by(step).copied().collect();
    let rate = rate as f32/step as f32;
    let mean = samples.iter().sum::<f32>()/samples.len() as f32;
    let samples:Vec<f32> = samples.iter().map(|x|x-mean).collect();
    if samples.iter().map(|x|x*x).sum::<f32>()/(samples.len() as f32) < 0.00001 { return None; }
    let lo = (rate/1200.0).max(2.0) as usize;
    let hi = ((rate/55.0) as usize).min(samples.len()/2);
    if hi<=lo+2 { return None; }
    let mut scores = vec![0.0;hi+1];
    for lag in lo..=hi {
        let (mut cross,mut aa,mut bb) = (0.0,0.0,0.0);
        for i in 0..samples.len()-lag {
            let a = samples[i]; let b = samples[i+lag];
            cross+=a*b; aa+=a*a; bb+=b*b;
        }
        scores[lag]=cross/(aa*bb).sqrt().max(1e-12);
    }
    let best = scores[lo..=hi].iter().copied().fold(-1.0,f32::max);
    if best<0.65 { return None; }
    let threshold = (best*0.92).max(0.65);
    let peak = (lo+1..hi).find(|&lag|scores[lag]>threshold && scores[lag]>=scores[lag-1] && scores[lag]>scores[lag+1])?;
    let (a,b,c)=(scores[peak-1],scores[peak],scores[peak+1]);
    let denom=a-2.0*b+c;
    let delta=if denom.abs()>1e-9 { (0.5*(a-c)/denom).clamp(-0.5,0.5) } else {0.0};
    Some(rate/(peak as f32+delta))
}

pub fn nearest_note(midi:f32, notes:&[bool;12]) -> Option<f32> {
    (-12..=12).map(|offset|midi.round() as i32+offset)
        .filter(|n|notes[n.rem_euclid(12) as usize])
        .min_by(|a,b|(*a as f32-midi).abs().total_cmp(&(*b as f32-midi).abs()))
        .map(|n|n as f32)
}

fn interpolate(audio:&Audio, frame:f32, channel:usize)->f32 {
    let frames=audio.samples.len()/audio.channels as usize;
    let frame=frame.clamp(0.0,frames.saturating_sub(1) as f32);
    let i=frame as usize;
    let j=(i+1).min(frames-1);
    let fraction=frame-i as f32;
    let ch=audio.channels as usize;
    audio.samples[i*ch+channel]*(1.0-fraction)+audio.samples[j*ch+channel]*fraction
}

/// Sustain direction follows the original messages; magnitude and timing are estimated.
fn smoothing_seconds(speed: f32, sustain: f32, held: f32) -> f32 {
    let sustain_factor=1.0-sustain.clamp(-100.0,100.0)/100.0*held.min(1.0)*0.9;
    (speed.max(0.0)/1000.0*sustain_factor).max(0.00001)
}

/// Duration-preserving pitch synchronous grains for offline monophonic vocals.
pub fn process(audio:&Audio, settings:&Settings)->Audio {
    if audio.samples.is_empty() || audio.channels==0 || audio.rate==0 || settings.bypass {
        return audio.clone();
    }
    let amount=settings.amount.clamp(0.0,100.0)/100.0;
    if (amount==0.0 || !settings.notes.iter().any(|x|*x)) && settings.shift==0.0 && settings.doubler==0.0 {
        return audio.clone();
    }
    let channels=audio.channels as usize;
    let frames=audio.samples.len()/channels;
    let mono:Vec<f32>=audio.samples.chunks_exact(channels).map(|f|f.iter().sum::<f32>()/channels as f32).collect();
    let grain=((audio.rate as f32*0.08) as usize).max(256);
    let analysis_hop=grain/4;
    let mut output=vec![0.0;audio.samples.len()];
    let mut weights=vec![0.0;frames];
    let (mut correction,mut previous_target,mut held)=(0.0,0.0,0.0_f32);
    let mut center_float=0.0_f32;
    let mut analysis_index=usize::MAX;
    let mut cached_pitch=None;
    while center_float < (frames+grain) as f32 {
        let center=center_float as usize;
        let mut period=analysis_hop as f32;
        let start=center.saturating_sub(grain/2).min(frames);
        let end=(start+grain).min(frames);
        let mut desired=0.0;
        if center/analysis_hop != analysis_index {
            analysis_index=center/analysis_hop;
            cached_pitch=detect_pitch(&mono[start..end],audio.rate);
        }
        if let Some(hz)=cached_pitch {
            period=audio.rate as f32/hz;
            let midi=69.0+12.0*(hz/440.0).log2();
            if let Some(mut target)=nearest_note(midi,&settings.notes) {
                if settings.stabilize && (midi-previous_target).abs()<0.65 && previous_target!=0.0 {
                    target=previous_target;
                }
                held=if target==previous_target {held+period/audio.rate as f32} else {0.0};
                previous_target=target;
                desired=(target-midi)*amount;
            }
        }
        let seconds=smoothing_seconds(settings.speed, settings.sustain, held);
        let blend=1.0-(-period/(audio.rate as f32*seconds)).exp();
        correction+=(desired-correction)*blend;
        let ratio=2_f32.powf((correction+settings.shift.clamp(-12.0,12.0))/12.0);
        // Align source grains to rising waveform crossings (pitch synchronous overlap).
        let radius=(period*1.5).max(16.0) as usize;
        let search_start=center.saturating_sub((period*0.6) as usize).min(frames.saturating_sub(1));
        let search_end=(center+(period*0.6) as usize).min(frames.saturating_sub(1));
        let epoch=(search_start..search_end).filter(|&i|mono[i]<=0.0 && mono[i+1]>0.0)
            .min_by_key(|&i|i.abs_diff(center)).map(|i|i as f32-mono[i]/(mono[i+1]-mono[i]))
            .unwrap_or(center as f32);
        for local in 0..radius*2 {
            let offset=local as f32-radius as f32;
            let position=center as isize+local as isize-radius as isize;
            if position<0 || position>=frames as isize {continue;}
            let position=position as usize;
            let window=(std::f32::consts::PI*local as f32/(radius*2) as f32).sin().powi(2);
            weights[position]+=window;
            for channel in 0..channels {
                output[position*channels+channel]+=interpolate(audio,epoch+offset,channel)*window;
            }
        }
        center_float+=(period/ratio).max(1.0);
    }
    for (frame,weight) in weights.iter().enumerate() {
        for channel in 0..channels {
            if *weight>1e-8 { output[frame*channels+channel]/=weight; }
        }
    }
    let mix=settings.doubler.clamp(0.0,100.0)/100.0*0.5;
    if mix>0.0 {
        let dry=output.clone();
        for frame in 0..frames {
            for channel in 0..channels {
                let width=settings.width.clamp(0.0,100.0)/100.0;
                let delay=audio.rate as f32*(0.018+if channel==0 {-0.006*width} else {0.006*width});
                let modulation=(frame as f32/audio.rate as f32*1.7).sin()*audio.rate as f32*0.001;
                let source=(frame as f32-delay-modulation).max(0.0) as usize;
                output[frame*channels+channel]=dry[frame*channels+channel]*(1.0-mix)+dry[source*channels+channel]*mix;
            }
        }
    }
    Audio { samples:output,rate:audio.rate,channels:audio.channels }
}
