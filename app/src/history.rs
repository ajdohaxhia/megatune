use crate::Controls;
#[cfg(test)]
mod tests {
 use super::*;
 #[test] fn undo_redo_restores_complete_settings_and_branches() {
  let original=Controls::default();let changed=Controls{speed:0.,bypass:true,..original.clone()};
  let mut history=History::default();history.record(original.clone());
  assert_eq!(history.undo(&changed),Some(original.clone()));
  assert_eq!(history.redo(&original),Some(changed.clone()));
  history.undo(&changed);history.record(original);assert!(history.redo(&changed).is_none());
 }
 #[test] fn comparison_keeps_both_editable_slots() {
  let a=Controls::default();let b=Controls{speed:0.,..a.clone()};let mut comparison=Comparison::default();
  assert_eq!(comparison.swap(&a),a);assert_eq!(comparison.swap(&b),a);assert_eq!(comparison.swap(&a),b);
 }
}
#[derive(Default)]
pub struct History {past:Vec<Controls>,future:Vec<Controls>}
impl History {
 pub fn record(&mut self,c:Controls){if self.past.last()!=Some(&c){self.past.push(c);if self.past.len()>100{self.past.remove(0);}}self.future.clear();}
 pub fn undo(&mut self,current:&Controls)->Option<Controls>{let previous=self.past.pop()?;self.future.push(current.clone());Some(previous)}
 pub fn redo(&mut self,current:&Controls)->Option<Controls>{let next=self.future.pop()?;self.past.push(current.clone());Some(next)}
 pub fn can_undo(&self)->bool{!self.past.is_empty()}
 pub fn can_redo(&self)->bool{!self.future.is_empty()}
}
#[derive(Default)]
pub struct Comparison {other:Option<Controls>,pub is_b:bool}
impl Comparison {
 pub fn swap(&mut self,current:&Controls)->Controls{let next=self.other.replace(current.clone()).unwrap_or_else(||current.clone());self.is_b=!self.is_b;next}
}
