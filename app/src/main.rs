mod history;

mod audio;
mod interface;
mod parameters;
mod presets;
use eframe::egui::{self,Color32,Vec2};
use serde::{Serialize,Deserialize};
const NOTES:[&str;12]=["C","C#","D","D#","E","F","F#","G","G#","A","A#","B"];
const CYAN:Color32=Color32::from_rgb(177,129,245);
#[derive(Clone,Debug,PartialEq,Serialize,Deserialize)]
struct Controls {speed:f32,sustain:f32,amount:f32,shift:f32,doubler:f32,width:f32,key:usize,scale:usize,notes:[bool;12],bypass:bool,stabilize:bool}
impl Default for Controls {fn default()->Self{Self{speed:parameters::DEFAULT_SPEED,sustain:parameters::DEFAULT_SUSTAIN,amount:parameters::DEFAULT_AMOUNT,shift:0.,doubler:parameters::DEFAULT_DOUBLER,width:parameters::DEFAULT_WIDTH,key:4,scale:2,notes:[true,false,true,false,true,false,true,true,false,true,false,true],bypass:false,stabilize:true}}}
struct App {c:Controls,original:Option<audio::Audio>,processed:Option<audio::Audio>,name:String,status:String,frames:u32,shot:Option<String>,dirty:Option<std::time::Instant>,pending:Option<std::sync::mpsc::Receiver<(Controls,audio::Audio)>>,stream:Option<rodio::OutputStream>,sink:Option<rodio::Sink>,show_help:bool,history:history::History,comparison:history::Comparison,restoring:bool,gesture_start:Option<Controls>}
impl App {
fn new(cc:&eframe::CreationContext<'_>)->Self {let mut style=(*cc.egui_ctx.style()).clone();style.visuals=egui::Visuals::dark();style.visuals.panel_fill=Color32::from_rgb(19,23,30);style.visuals.widgets.active.bg_fill=CYAN;style.visuals.selection.bg_fill=Color32::from_rgb(92,61,140);style.spacing.item_spacing=Vec2::new(12.,10.);cc.egui_ctx.set_style(style); Self{c:Controls::default(),original:None,processed:None,name:"No audio loaded".into(),status:"Open a vocal WAV to begin".into(),frames:0,shot:std::env::args().skip_while(|a|a!="--screenshot").nth(1),dirty:None,pending:None,stream:None,sink:None,show_help:false,history:history::History::default(),comparison:history::Comparison::default(),restoring:false,gesture_start:None}}
fn settings(&self)->audio::Settings{audio::Settings{speed:self.c.speed,sustain:self.c.sustain,amount:self.c.amount,notes:self.c.notes,shift:self.c.shift,doubler:self.c.doubler,width:self.c.width,bypass:self.c.bypass,stabilize:self.c.stabilize}}
fn apply(&mut self){
 if let Some(a)=self.original.clone(){let settings=self.settings();let controls=self.c.clone();let(tx,rx)=std::sync::mpsc::channel();self.pending=Some(rx);self.dirty=None;self.status="Processing vocal…".into();std::thread::spawn(move||{let result=audio::process(&a,&settings);let _=tx.send((controls,result));});}
}
fn controls_changed(&mut self){
 self.processed=None;
 if let Some(s)=self.sink.take(){s.stop();}
 self.dirty=self.original.as_ref().map(|_|std::time::Instant::now());
 if self.original.is_some(){self.status="Updating tuning…".into();}
}
fn play(&mut self,processed:bool){if let Some(s)=&self.sink{s.stop();}let data=if processed{self.processed.as_ref()}else{self.original.as_ref()};if let Some(a)=data {match rodio::OutputStream::try_default(){Ok((stream,handle))=>match rodio::Sink::try_new(&handle){Ok(sink)=>{sink.append(rodio::buffer::SamplesBuffer::new(a.channels,a.rate,a.samples.clone()));self.stream=Some(stream);self.sink=Some(sink);self.status=if processed{"Playing corrected vocal"}else{"Playing original vocal"}.into();},Err(e)=>self.status=e.to_string()},Err(e)=>self.status=e.to_string()}}}
}
impl eframe::App for App {fn update(&mut self,ctx:&egui::Context,_:&mut eframe::Frame){self.frames=self.frames.wrapping_add(1);ctx.request_repaint_after(std::time::Duration::from_millis(33));let before=self.c.clone();if let Some(rx)=&self.pending {match rx.try_recv(){Ok((controls,a))=>{self.pending=None;if controls==self.c{self.processed=Some(a);self.status="Correction ready · preview or export your vocal".into();}},Err(std::sync::mpsc::TryRecvError::Disconnected)=>{self.pending=None;self.status="Audio processing stopped. Please try again.".into();},Err(std::sync::mpsc::TryRecvError::Empty)=>{}}}
for e in ctx.input(|i|i.events.clone()){if let egui::Event::Screenshot{image,..}=e {if let Some(path)=&self.shot{let bytes:Vec<u8>=image.pixels.iter().flat_map(|c|c.to_array()).collect();if let Some(parent)=std::path::Path::new(path).parent(){let _=std::fs::create_dir_all(parent);}match image::save_buffer(path,&bytes,image.size[0] as u32,image.size[1] as u32,image::ColorType::Rgba8){Ok(_)=>ctx.send_viewport_cmd(egui::ViewportCommand::Close),Err(e)=>{self.status=e.to_string();ctx.send_viewport_cmd(egui::ViewportCommand::Close);}}}}}
if self.frames==120 && self.shot.is_some(){ctx.send_viewport_cmd(egui::ViewportCommand::Screenshot(egui::UserData::default()));}
egui::TopBottomPanel::top("header").exact_height(38.).show(ctx,|ui|{ui.horizontal_centered(|ui|{ui.add_space(4.);
if ui.add_enabled(self.history.can_undo(),egui::Button::new("Undo")).on_hover_text("Undo").clicked(){if let Some(c)=self.history.undo(&self.c){self.c=c;self.restoring=true;}}
if ui.add_enabled(self.history.can_redo(),egui::Button::new("Redo")).on_hover_text("Redo").clicked(){if let Some(c)=self.history.redo(&self.c){self.c=c;self.restoring=true;}}
if ui.button(if self.comparison.is_b{"B / A"}else{"A / B"}).on_hover_text("Switch between two editable settings").clicked(){self.c=self.comparison.swap(&self.c);self.restoring=true;}
let preset_index=presets::PRESETS.iter().position(|p|self.c.speed==p.1&&self.c.sustain==p.2&&self.c.amount==p.3&&self.c.doubler==p.4&&self.c.width==p.5).unwrap_or(0);
for (label,offset) in [("‹",presets::PRESETS.len()-1),("›",1)] {if ui.button(label).on_hover_text("Previous / next factory preset").clicked(){let p=presets::PRESETS[(preset_index+offset)%presets::PRESETS.len()];self.c=Controls{speed:p.1,sustain:p.2,amount:p.3,doubler:p.4,width:p.5,..self.c.clone()};}}
egui::ComboBox::from_id_salt("factory-presets").selected_text(presets::PRESETS.iter().find(|(_,speed,sustain,amount,doubler,width)| self.c.speed==*speed && self.c.sustain==*sustain && self.c.amount==*amount && self.c.doubler==*doubler && self.c.width==*width).map(|p|p.0).unwrap_or("Custom settings")).show_ui(ui,|ui|{for (name,speed,sustain,amount,doubler,width) in presets::PRESETS {if ui.selectable_label(false,*name).clicked(){self.c=Controls{speed:*speed,sustain:*sustain,amount:*amount,doubler:*doubler,width:*width,..self.c.clone()};}}});ui.label(egui::RichText::new("MegaTune").size(23.).strong().color(CYAN));ui.label(egui::RichText::new("").size(10.).color(Color32::GRAY));ui.with_layout(egui::Layout::right_to_left(egui::Align::Center),|ui|{if ui.button("?").clicked(){self.show_help=!self.show_help;}ui.toggle_value(&mut self.c.bypass,"Bypass");if ui.button("Save preset").clicked(){if let Some(p)=rfd::FileDialog::new().add_filter("Preset",&["json"]).save_file(){self.status=serde_json::to_string_pretty(&self.c).map_err(|e|e.to_string()).and_then(|s|std::fs::write(p,s).map_err(|e|e.to_string())).map(|_|"Preset saved".into()).unwrap_or_else(|e|e);}}if ui.button("Load preset").clicked(){if let Some(p)=rfd::FileDialog::new().add_filter("Preset",&["json"]).pick_file(){match std::fs::read_to_string(p).ok().and_then(|s|serde_json::from_str::<Controls>(&s).ok()){Some(mut c)=>{c.key=c.key.min(11);c.scale=c.scale.min(4);c.speed=c.speed.clamp(-3.,200.);c.sustain=c.sustain.clamp(-100.,100.);c.amount=c.amount.clamp(0.,100.);c.shift=c.shift.clamp(-12.,12.);c.doubler=c.doubler.clamp(0.,100.);c.width=c.width.clamp(0.,100.);self.c=c;self.status="Preset loaded".into();},None=>self.status="Could not read that preset".into()}}}});});});
egui::TopBottomPanel::bottom("footer").exact_height(38.).show(ctx,|ui|{ui.horizontal_centered(|ui|{ui.colored_label(CYAN,"•");ui.label(&self.status);});});
egui::CentralPanel::default().show(ctx,|ui|{interface::draw(ui,&mut self.c,self.original.as_ref(), self.processed.as_ref(), ctx.input(|i|i.time) as f32, self.sink.as_ref().filter(|s|!s.empty()).map(|s|s.get_pos().as_secs_f32()));ui.add_space(8.);
ui.horizontal(|ui|{if ui.add_enabled(self.pending.is_none(),egui::Button::new("Open vocal")).clicked(){if let Some(path)=rfd::FileDialog::new().add_filter("Wave audio",&["wav"]).pick_file(){match audio::load(&path){Ok(a)=>{self.name=path.file_name().unwrap_or_default().to_string_lossy().into();self.status=format!("{:.1} seconds · {} Hz · {} channels",a.samples.len() as f32/a.rate as f32/a.channels as f32,a.rate,a.channels);if let Some(s)=self.sink.take(){s.stop();}self.pending=None;self.original=Some(a);self.processed=None;self.dirty=Some(std::time::Instant::now());},Err(e)=>self.status=e}}}ui.label(&self.name);});ui.add_space(8.);ui.horizontal(|ui|{if ui.add_enabled(self.original.is_some(),egui::Button::new("▶ Original")).clicked(){self.play(false);}if ui.add_enabled(self.original.is_some()&&self.pending.is_none(),egui::Button::new("Apply tuning").fill(Color32::from_rgb(92,61,140))).clicked(){self.apply();}if ui.add_enabled(self.processed.is_some(),egui::Button::new("▶ Corrected")).clicked(){self.play(true);}if ui.button("Stop").clicked(){if let Some(s)=self.sink.take(){s.stop();}self.status="Playback stopped".into();}if ui.add_enabled(self.processed.is_some(),egui::Button::new("Export WAV ↗")).clicked(){if let Some(path)=rfd::FileDialog::new().add_filter("Wave audio",&["wav"]).set_file_name("tuned-vocal.wav").save_file(){self.status=audio::save(&path,self.processed.as_ref().unwrap()).map(|_|"Corrected vocal exported".into()).unwrap_or_else(|e|e);}}});});
if self.c!=before{if !self.restoring{if ctx.input(|i|i.pointer.primary_down()){if self.gesture_start.is_none(){self.gesture_start=Some(before);}}else{self.history.record(before);}}self.controls_changed();}
if !ctx.input(|i|i.pointer.primary_down()){if let Some(start)=self.gesture_start.take(){if start!=self.c{self.history.record(start);}}}self.restoring=false;
if self.pending.is_none() && self.dirty.is_some_and(|t|t.elapsed().as_millis()>=250){self.apply();}
if self.show_help{egui::Window::new("Using MegaTune").open(&mut self.show_help).show(ctx,|ui|{ui.label("Choose a key and scale, or select individual notes. Open a vocal WAV, adjust the controls, and apply tuning. Compare both previews, then export.");ui.label("Drag knobs vertically; double-click to reset. Low speed gives firmer correction. Sustain changes correction on held notes. The doubler adds a wider second voice.");ui.label("For your DAW: run Install MegaTune.command beside the app, then restart or rescan VST3 plug-ins and add MegaTune as an audio effect on your vocal track. The DAW saves your settings with its project.");});}
}}
fn main()->eframe::Result {eframe::run_native("MegaTune",eframe::NativeOptions{viewport:egui::ViewportBuilder::default().with_inner_size([900.,800.]).with_min_inner_size([860.,760.]),..Default::default()},Box::new(|cc|Ok(Box::new(App::new(cc)))))}

#[cfg(test)]
mod control_tests {
 use super::*;
 #[test]
 fn settings_forward_every_switch_and_effect(){
  let c=Controls{bypass:true,stabilize:false,shift:7.,doubler:65.,width:80.,..Controls::default()};
  let encoded=serde_json::to_string(&c).unwrap();
  assert_eq!(serde_json::from_str::<Controls>(&encoded).unwrap(),c);
 }
}
