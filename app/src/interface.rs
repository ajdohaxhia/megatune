use crate::{audio, parameters, Controls, NOTES};
use eframe::egui::{self, Align2, Color32 as C, FontId, Pos2, Rect, Sense, Stroke, Vec2};
const PURPLE:C=C::from_rgb(171,126,247);
fn text(p:&egui::Painter, at:Pos2, label:&str, size:f32,color:C){p.text(at,Align2::CENTER_CENTER,label,FontId::proportional(size),color);}
fn knob(ui:&mut egui::Ui,center:Pos2,r:f32,label:&str,value:&mut f32,mapping:(fn(f32)->f32,fn(f32)->f32),reset:f32){
 let response=ui.interact(Rect::from_center_size(center,Vec2::splat(r*2.)),ui.id().with(label),Sense::click_and_drag());
 let (to_normalized,from_normalized)=mapping;
 if response.dragged(){let delta=ui.input(|i|i.pointer.delta().y);*value=from_normalized((to_normalized(*value)-delta/250.).clamp(0.,1.));}
 if response.double_clicked(){*value=reset;}
 let p=ui.painter();p.circle_filled(center+Vec2::new(2.,4.),r+2.,C::from_gray(12));p.circle_filled(center,r,C::from_gray(32));p.circle_stroke(center,r-3.,Stroke::new(2_f32,C::from_gray(48)));p.circle_filled(center,r*0.78,C::from_gray(28));
 let t=to_normalized(*value);
 for i in 0..100{let a=2.2+i as f32/99.*4.9;let c=if (i as f32/100.)<t{C::from_rgb(190,133+(i/3) as u8,204)}else{C::from_gray(46)};p.line_segment([center+Vec2::angled(a)*r*0.84,center+Vec2::angled(a)*r*0.95],Stroke::new(2_f32,c));}
 text(p,center-Vec2::new(0.,7.),&format!("{:.1}",*value),r*0.32,C::from_gray(226));text(p,center+Vec2::new(0.,r*0.28),label,r*0.16,C::from_gray(192));response.on_hover_text("Drag up or down. Double-click to reset.");
}
pub(super) fn draw(ui:&mut egui::Ui,c:&mut Controls,a:Option<&audio::Audio>,output:Option<&audio::Audio>,seconds:f32,playback:Option<f32>){
 let w=ui.available_width();let h=w*0.69;let (rect,_)=ui.allocate_exact_size(Vec2::new(w,h),Sense::hover());let scale=w/860.;let at=|x:f32,y:f32|rect.min+Vec2::new(x,y)*scale;
 let p=ui.painter().clone();p.rect_filled(rect,0.,C::from_gray(25));
 p.add(egui::Shape::convex_polygon(vec![at(430.,0.),at(860.,0.),at(860.,384.),at(680.,384.),at(610.,330.)],C::from_gray(35),Stroke::NONE));
 let center=at(291.,185.);let radius=142.*scale;
 for i in (0..75).rev(){let t=i as f32/75.;p.circle_filled(center,radius*t,C::from_rgb((15.+9.*t) as u8,(13.+8.*t) as u8,(20.+10.*t) as u8));}
 // Soft, faceted violet core, rotating independently of the outer particle shell.
 for layer in (0..48).rev(){let t=layer as f32/48.;p.circle_filled(center,65.*scale*t,C::from_rgb((49.+35.*(1.-t)) as u8,(31.+10.*(1.-t)) as u8,(73.+45.*(1.-t)) as u8));}
 for face in 0..7{let angle=face as f32*std::f32::consts::TAU/7.+seconds*0.07;let next=angle+std::f32::consts::TAU/7.;p.add(egui::Shape::convex_polygon(vec![center+Vec2::new(-12.,8.)*scale,center+Vec2::angled(angle)*61.*scale,center+Vec2::angled(next)*61.*scale],C::from_rgba_unmultiplied(167,105,210,12+face*4),Stroke::NONE));}
 for i in 0..10500_u32 {
 let hash=i.wrapping_mul(747796405).wrapping_add(2891336453);let hash2=(hash^(hash>>16)).wrapping_mul(2246822519);
 let angle=(hash%65536) as f32/65536.*std::f32::consts::TAU+seconds*0.035;
 let radial=(hash2%65536) as f32/65536.;let rr=radial.sqrt()*radius;
 let edge=(rr/radius).powf(14.);let core=(1.-(rr/(65.*scale)).min(1.)).powf(0.4);
 let alpha=((edge*105.+core*38.)*((hash2>>16)%100) as f32/100.) as u8;
 if alpha>3 {p.circle_filled(center+Vec2::angled(angle)*rr,0.55*scale,C::from_rgba_unmultiplied(if core>0.{175}else{240},if core>0.{120}else{236},250,alpha));}
 }
 let activity=playback.and_then(|seconds|a.map(|a|{let start=(seconds*a.rate as f32) as usize*a.channels as usize;let end=(start+1024).min(a.samples.len());if start<end{a.samples[start..end].iter().map(|s|s*s).sum::<f32>()/(end-start) as f32}else{0.}})).unwrap_or(0.).sqrt();
 let time=seconds*0.8;
 let pulse=if c.bypass{0.}else{activity.min(1.)*10.};
 for ring in 0..4{let rr=([141.,92.,65.,58.][ring]+pulse*(ring as f32)*0.7)*scale;let color=[C::from_gray(220),C::from_rgb(116,84,181),PURPLE,C::from_rgb(212,181,245)][ring];
 for layer in 0..5{let points=(0..=240).map(|i|{let angle=i as f32/240.*std::f32::consts::TAU;let ripple=if ring>=2{(angle*7.+time).sin()*2.5+(angle*13.-time).sin()*1.5}else{(angle*11.+time).sin()*0.5};center+Vec2::angled(angle)*(rr+ripple*scale+layer as f32*scale*0.6)}).collect();p.add(egui::Shape::line(points,Stroke::new((1.-layer as f32*0.15)*scale,color.gamma_multiply(1.-layer as f32*0.17))));}}
 text(&p,at(293.,362.),if c.bypass{"BYPASSED"}else if playback.is_some(){"VOCAL ENGINE · PLAYING"}else{"MEGATUNE  /  VOCAL ENGINE"},10.*scale,C::from_gray(104));
 knob(ui,at(672.,113.),79.*scale,"Speed",&mut c.speed,(parameters::speed_to_normalized,parameters::speed_from_normalized),25.);
 knob(ui,at(783.,143.),49.*scale,"Sustain",&mut c.sustain,(parameters::sustain_to_normalized,parameters::sustain_from_normalized),parameters::DEFAULT_SUSTAIN);
 knob(ui,at(756.,268.),69.*scale,"Amount",&mut c.amount,(parameters::amount_to_normalized,parameters::amount_from_normalized),100.);
 // Scale strip, kept as native controls for keyboard access.
 ui.scope_builder(egui::UiBuilder::new().max_rect(Rect::from_min_max(at(10.,390.),at(850.,425.))),|ui|{ui.horizontal(|ui|{
 ui.label("Select");let old=(c.key,c.scale);
 egui::ComboBox::from_id_salt("root").selected_text(NOTES[c.key]).width(45.).show_ui(ui,|ui|{for(i,n)in NOTES.iter().enumerate(){ui.selectable_value(&mut c.key,i,*n);}});
 for (i,name) in ["Chromatic","Major","Minor"].iter().enumerate(){ui.selectable_value(&mut c.scale,i,*name);}
 if old!=(c.key,c.scale){let intervals:Vec<usize>=match c.scale{1=>vec![0,2,4,5,7,9,11],2=>vec![0,2,3,5,7,8,10],3=>vec![0,2,3,5,7,8,11],4=>vec![0,2,4,7,9],_=>(0..12).collect()};c.notes=std::array::from_fn(|i|intervals.contains(&((i+12-c.key)%12)));}
 egui::ComboBox::from_id_salt("extra-scales").selected_text("More scales").show_ui(ui,|ui|{for(i,label) in [(3,"Harmonic minor"),(4,"Pentatonic")]{if ui.selectable_value(&mut c.scale,i,label).clicked(){let intervals:Vec<usize>=if i==3{vec![0,2,3,5,7,8,11]}else{vec![0,2,4,7,9]};c.notes=std::array::from_fn(|n|intervals.contains(&((n+12-c.key)%12)));}}});ui.checkbox(&mut c.stabilize,"Note stabilizer");ui.checkbox(&mut c.bypass,"Bypass");});});
 // Actual audio activity, no simulated input meter.
 text(&p,at(40.,438.),"INPUT",10.*scale,C::GRAY);text(&p,at(40.,456.),"OUTPUT",10.*scale,C::GRAY);
 if let Some(a)=a{for i in 0..120{let idx=i*a.samples.len()/120;let amp=a.samples[idx].abs().min(1.);let x=108.+i as f32*5.4;p.line_segment([at(x,439.-amp*7.),at(x,439.+amp*7.)],Stroke::new(2.*scale,PURPLE));}}
 if let Some(a)=output{for i in 0..120{let idx=i*a.samples.len()/120;let amp=a.samples[idx].abs().min(1.);let x=108.+i as f32*5.4;p.line_segment([at(x,456.-amp*7.),at(x,456.+amp*7.)],Stroke::new(2.*scale,PURPLE));}}
 if let (Some(position),Some(audio))=(playback,a){let duration=audio.samples.len() as f32/audio.channels as f32/audio.rate as f32;if duration>0.{let x=103.+654.*(position/duration).clamp(0.,1.);p.line_segment([at(x,430.),at(x,466.)],Stroke::new(2.*scale,C::WHITE));}}
 p.line_segment([at(103.,467.),at(757.,467.)],Stroke::new(1_f32,C::from_gray(58)));
 text(&p,at(46.,512.),NOTES[c.key],47.*scale,C::from_gray(220));text(&p,at(46.,555.),["chromatic","major","minor","harmonic","pentatonic"][c.scale],13.*scale,C::GRAY);
 let whites=[0,2,4,5,7,9,11];let keyw=654./7.;
 for (j,i) in whites.iter().enumerate(){let r=Rect::from_min_max(at(103.+j as f32*keyw,468.),at(103.+(j+1) as f32*keyw-1.,590.));let on=c.notes[*i];let response=ui.interact(r,ui.id().with(("white",i)),Sense::click());p.rect_filled(r,2.,if on{PURPLE}else{C::from_gray(91)});if on{for row in 0..60{let t=row as f32/60.;let band=Rect::from_min_max(Pos2::new(r.left(),r.top()+r.height()*t),Pos2::new(r.right(),r.top()+r.height()*(t+1./60.)));p.rect_filled(band,0.,C::from_rgb((160.-t*26.) as u8,(116.+t*11.) as u8,(242.-t*56.) as u8));}}text(&p,at(103.+(j as f32+0.5)*keyw,488.),NOTES[*i],11.*scale,C::WHITE);if response.clicked(){c.notes[*i]=!on;}}
 for (j,i) in [(0,1),(1,3),(3,6),(4,8),(5,10)]{let x=103.+(j as f32+1.)*keyw;let r=Rect::from_min_max(at(x-26.,468.),at(x+26.,547.));let on=c.notes[i];let response=ui.interact(r,ui.id().with(("black",i)),Sense::click());p.rect_filled(r,5.,if on{C::from_rgb(81,57,125)}else{C::from_gray(28)});p.rect_stroke(r,5.,Stroke::new(2_f32,C::from_gray(16)),egui::StrokeKind::Inside);text(&p,Pos2::new(r.center().x,r.top()+19.*scale),NOTES[i],10.*scale,C::LIGHT_GRAY);if response.clicked(){c.notes[i]=!on;}}
 ui.scope_builder(egui::UiBuilder::new().max_rect(Rect::from_min_max(at(765.,475.),at(855.,590.))),|ui|{ui.spacing_mut().slider_width=65.*scale;ui.style_mut().spacing.item_spacing.y=2.;
ui.label("Transpose");ui.add(egui::Slider::new(&mut c.shift,-12.0..=12.0).show_value(false)).on_hover_text(format!("{:.1} semitones",c.shift));
ui.label("Doubler");ui.add(egui::Slider::new(&mut c.doubler,0.0..=100.0).show_value(false)).on_hover_text(format!("{:.0}%",c.doubler));
ui.label("Width");ui.add(egui::Slider::new(&mut c.width,0.0..=100.0).show_value(false)).on_hover_text(format!("{:.0}%",c.width));});
}
