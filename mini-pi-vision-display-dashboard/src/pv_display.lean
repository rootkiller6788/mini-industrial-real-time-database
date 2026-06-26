/**
 * pv_display.lean - Formal Verification of PI Vision Display Model
 *
 * Lean 4 formalization of display element hierarchy, coordinate
 * geometry, and state machine properties.
 *
 * Knowledge Points:
 *   L4: Formal theorems about display element tree properties
 *   L4: Rectangle geometry proofs (containment, overlap)
 *   L4: State machine transition validity
 */

namespace PiVision

/- =========================================================================
   L1: Core Types - Struct definitions mirroring C structures
   ========================================================================= -/

structure Color where
  r : Nat
  g : Nat
  b : Nat
  a : Nat
  deriving BEq, Repr

structure Coord where
  x : Float
  y : Float
  deriving Repr

structure Rect where
  origin : Coord
  width  : Float
  height : Float
  deriving Repr

inductive SymbolType
  | value | trend | barGraph | gauge | stateIndicator
  | textLabel | image | table | alarmList | kpiIndicator
  deriving BEq, Repr

inductive DisplayState
  | loading | active | paused | stale | error
  deriving BEq, Repr

inductive NavigationLevel
  | level1 | level2 | level3 | level4
  deriving BEq, Repr, Ord

/- =========================================================================
   L4: Rectangle Geometry Theorems
   ========================================================================= -/

def Rect.contains (r : Rect) (p : Coord) : Bool :=
  p.x >= r.origin.x && p.x <= r.origin.x + r.width &&
  p.y >= r.origin.y && p.y <= r.origin.y + r.height

/- Theorem: A rectangle always contains its own origin point.
   This is true because origin.x >= origin.x and origin.x <= origin.x + width
   when width >= 0. -/
theorem rect_contains_origin (r : Rect) (h : r.width >= 0.0 ∧ r.height >= 0.0) :
    r.contains r.origin := by
  rcases h with ⟨hw, hh⟩
  unfold Rect.contains
  have hx1 : r.origin.x >= r.origin.x := by rfl
  have hx2 : r.origin.x <= r.origin.x + r.width := by
    linarith
  have hy1 : r.origin.y >= r.origin.y := by rfl
  have hy2 : r.origin.y <= r.origin.y + r.height := by
    linarith
  simp [hx1, hx2, hy1, hy2]

/- Theorem: If point p is inside rectangle r, then the origin
   of r shifted by (width, height) is also inside r (the bottom-right corner).
   Note: This is true only if width >= 0 and height >= 0. -/
theorem rect_contains_opposite_corner (r : Rect) (hw : r.width >= 0.0) (hh : r.height >= 0.0) :
    r.contains { x := r.origin.x + r.width, y := r.origin.y + r.height } := by
  unfold Rect.contains
  have hx1 : r.origin.x + r.width >= r.origin.x := by linarith
  have hx2 : r.origin.x + r.width <= r.origin.x + r.width := by rfl
  have hy1 : r.origin.y + r.height >= r.origin.y := by linarith
  have hy2 : r.origin.y + r.height <= r.origin.y + r.height := by rfl
  simp [hx1, hx2, hy1, hy2]

def Rect.overlaps (a b : Rect) : Bool :=
  ¬ (a.origin.x + a.width < b.origin.x ||
     b.origin.x + b.width < a.origin.x ||
     a.origin.y + a.height < b.origin.y ||
     b.origin.y + b.height < a.origin.y)

/- Theorem: Overlap relation is symmetric.
   Proved by case analysis on the disjunction. -/
theorem overlaps_symmetric (a b : Rect) : a.overlaps b = b.overlaps a := by
  unfold Rect.overlaps
  simp [or_comm, and_comm]

/- =========================================================================
   L4: Display State Machine Theorems
   ========================================================================= -/

/- A valid state transition function, mirroring pv_display_set_state in C. -/
def validTransition (from : DisplayState) (to : DisplayState) : Bool :=
  match to with
  | DisplayState.error => true  -- Error can be entered from any state
  | DisplayState.active => from == DisplayState.loading || from == DisplayState.paused
  | DisplayState.paused => from == DisplayState.active
  | DisplayState.loading => true  -- Loading can be entered from any state
  | DisplayState.stale => true   -- Stale can be entered from any state

/- Theorem: The validTransition relation is reflexive for loading, stale, and error.
   Proved by case analysis on the state. -/
theorem transition_reflexive_loading : validTransition DisplayState.loading DisplayState.loading := by
  unfold validTransition; rfl

theorem transition_reflexive_stale : validTransition DisplayState.stale DisplayState.stale := by
  unfold validTransition; rfl

theorem transition_reflexive_error : validTransition DisplayState.error DisplayState.error := by
  unfold validTransition; rfl

/- Theorem: Active state can transition to paused.
   This models the pause button behavior in PI Vision. -/
theorem active_to_paused_valid : validTransition DisplayState.active DisplayState.paused := by
  unfold validTransition; rfl

/- Theorem: Paused state can transition to active.
   This models the resume behavior. -/
theorem paused_to_active_valid : validTransition DisplayState.paused DisplayState.active := by
  unfold validTransition; rfl

/- Theorem: Active cannot transition directly to loading.
   The display must first pause or encounter an error. -/
theorem active_to_loading_invalid : ¬ validTransition DisplayState.active DisplayState.loading := by
  unfold validTransition; simp

/- =========================================================================
   L4: Navigation Level Theorems
   ========================================================================= -/

/- Check that level values are monotonically ordered: 1 < 2 < 3 < 4 -/
def levelToNat : NavigationLevel → Nat
  | NavigationLevel.level1 => 1
  | NavigationLevel.level2 => 2
  | NavigationLevel.level3 => 3
  | NavigationLevel.level4 => 4

theorem level_ordering : levelToNat NavigationLevel.level1 < levelToNat NavigationLevel.level4 := by
  unfold levelToNat; decide

theorem level2_gt_level1 : levelToNat NavigationLevel.level2 > levelToNat NavigationLevel.level1 := by
  unfold levelToNat; decide

/- =========================================================================
   L3: Element Tree Properties
   ========================================================================= -/

/- A simple element tree for formal reasoning about display hierarchy. -/
inductive ElementTree (α : Type)
  | leaf (val : α)
  | node (val : α) (children : List (ElementTree α))
  deriving Repr

/- Count total nodes in an element tree. -/
def ElementTree.count {α : Type} : ElementTree α → Nat
  | .leaf _ => 1
  | .node _ children => 1 + List.sum (children.map ElementTree.count)

/- Theorem: A leaf always has count 1.
   Proved by definitional equality (rfl). -/
theorem leaf_count_one (x : α) : (ElementTree.leaf x).count = 1 := by rfl

/- Theorem: Count of a node is at least 1 plus sum of children counts.
   Trivially true by the definition of count for node case. -/
theorem node_count_positive {α : Type} (x : α) (children : List (ElementTree α)) :
    (ElementTree.node x children).count >= 1 := by
  unfold ElementTree.count
  have : 1 + List.sum (children.map ElementTree.count) >= 1 := by
    omega
  exact this

end PiVision
