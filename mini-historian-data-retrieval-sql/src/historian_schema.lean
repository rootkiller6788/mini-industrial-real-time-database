/-
  historian_schema.lean -- Lean 4 Formalization of Industrial Historian Data Model

  Formalizes the core invariants of time-series data retrieval:
  - Timestamp total ordering
  - Quality flag semantics
  - Aggregation monotonicity
  - Compression correctness bounds

  Knowledge Coverage:
    L1: Timestamp, data point, quality definitions as Lean inductive types
    L3: Result set ordering property
    L4: Formal theorems about historian operations
-/

namespace HistorianSchema

/- L1: Timestamp and Data Quality Definitions -/

structure Timestamp where
  epoch_ms : Nat
  deriving DecidableEq, Repr

def Timestamp.le (a b : Timestamp) : Prop := a.epoch_ms <= b.epoch_ms
def Timestamp.lt (a b : Timestamp) : Prop := a.epoch_ms < b.epoch_ms

theorem timestamp_lt_trans (a b c : Timestamp)
    (h1 : a.lt b) (h2 : b.lt c) : a.lt c := by
  unfold Timestamp.lt at *
  have h := Nat.lt_of_lt_of_le h1 (Nat.le_of_lt h2)
  exact h

theorem timestamp_le_antisymm (a b : Timestamp)
    (h1 : a.le b) (h2 : b.le a) : a = b := by
  unfold Timestamp.le at *
  have heq : a.epoch_ms = b.epoch_ms := Nat.le_antisymm h1 h2
  cases a; cases b; simp at heq; simp [heq]

inductive Quality
  | good
  | uncertain
  | bad
  deriving DecidableEq, Repr, Inhabited

def Quality.isGood : Quality -> Bool
  | Quality.good      => true
  | Quality.uncertain => false
  | Quality.bad       => false

theorem only_good_passes_quality_check (q : Quality) :
    q.isGood = true <-> q = Quality.good := by
  constructor
  . intro h
    cases q
    . rfl
    . simp [Quality.isGood] at h
    . simp [Quality.isGood] at h
  . intro h; subst h; rfl

/- L3: Data Point and Result Set -/

structure DataPoint where
  ts : Timestamp
  val : Float
  qual : Quality
  deriving Repr

abbrev ResultSet := List DataPoint

theorem result_set_induction (P : ResultSet -> Prop)
    (h_nil : P [])
    (h_cons : forall (dp : DataPoint) (rs : ResultSet), P rs -> P (dp :: rs))
    (rs : ResultSet) : P rs := by
  induction rs with
  | nil => exact h_nil
  | cons dp rs ih => exact h_cons dp rs ih

/- L3: Sorted Result Set (Time-Ordered) -/

def is_time_ordered : ResultSet -> Prop
  | [] => True
  | [_] => True
  | dp1 :: dp2 :: rest =>
      (dp1.ts.epoch_ms <= dp2.ts.epoch_ms) /\ is_time_ordered (dp2 :: rest)

theorem append_preserves_order (rs : ResultSet) (dp : DataPoint) :
    is_time_ordered rs ->
    ((forall dp' in rs, dp'.ts.epoch_ms <= dp.ts.epoch_ms) ->
     is_time_ordered (rs ++ [dp])) := by
  intro h_order h_latest
  induction rs with
  | nil => simp [is_time_ordered]
  | cons hd tl ih =>
      simp [List.append]
      cases tl with
      | nil =>
          simp [is_time_ordered]
          have hmem := h_latest hd (by simp)
          exact hmem
      | cons hd2 tltl =>
          simp [is_time_ordered]
          rcases h_order with (h_hd, h_tl)
          have ih_res := ih h_tl
          exact And.intro h_hd (ih_res (by
            intro dp' hmem
            apply h_latest
            simp [hmem]))

theorem empty_is_time_ordered : is_time_ordered ([] : ResultSet) := by trivial

theorem singleton_is_time_ordered (dp : DataPoint) : is_time_ordered [dp] := by
  simp [is_time_ordered]

/- L5: Aggregation Properties -/

def aggregate_count (rs : ResultSet) : Nat := rs.length

theorem count_non_negative (rs : ResultSet) : aggregate_count rs >= 0 := by
  unfold aggregate_count; omega

theorem count_zero_iff_empty (rs : ResultSet) : aggregate_count rs = 0 <-> rs = [] := by
  unfold aggregate_count
  constructor
  . intro h; cases rs with
    | nil => rfl
    | cons _ _ => simp at h
  . intro h; subst h; rfl

theorem count_append_increases (rs : ResultSet) (dp : DataPoint) :
    aggregate_count (rs ++ [dp]) = aggregate_count rs + 1 := by
  unfold aggregate_count; simp [List.length_append]

/- L5: Compression Correctness Properties -/

def value_approx (v1 v2 : Float) (epsilon : Float) : Prop :=
  (v1 - v2).abs <= epsilon

def archive_covers (compressed original : ResultSet) (deviation : Float) : Prop :=
  forall (p : DataPoint), p in original ->
    exists (c : DataPoint), c in compressed /\ c.ts = p.ts /\ value_approx c.val p.val deviation

theorem archive_covers_self (rs : ResultSet) : archive_covers rs rs 0.0 := by
  intro p hmem; refine (p, hmem, rfl, ?_)
  unfold value_approx; simp

theorem archive_covers_ts_trans (c p1 p2 : ResultSet) (d1 d2 : Float)
    (h1 : archive_covers c p1 d1) (h2 : archive_covers p1 p2 d2) :
    (forall (p : DataPoint), p in p2 ->
      exists (cpt : DataPoint), cpt in c /\ cpt.ts = p.ts) := by
  intro p hmem_p2
  rcases h2 p hmem_p2 with (p1p, hp1p_in, hp1p_ts, hp1p_val)
  rcases h1 p1p hp1p_in with (cp, cp_in, cp_ts, cp_val)
  refine (cp, cp_in, ?_)
  calc
    cp.ts = p1p.ts := cp_ts
    _ = p.ts := hp1p_ts

/- L4: Timestamp Difference Properties -/

def timestamp_diff_ms (a b : Timestamp) : Nat :=
  if a.epoch_ms >= b.epoch_ms then a.epoch_ms - b.epoch_ms
  else b.epoch_ms - a.epoch_ms

theorem timestamp_diff_self_zero (t : Timestamp) : timestamp_diff_ms t t = 0 := by
  unfold timestamp_diff_ms
  have h : t.epoch_ms >= t.epoch_ms := Nat.le_refl _
  simp [h]; exact Nat.sub_self _

theorem timestamp_diff_symmetric (a b : Timestamp) :
    timestamp_diff_ms a b = timestamp_diff_ms b a := by
  unfold timestamp_diff_ms
  by_cases h : a.epoch_ms >= b.epoch_ms
  . simp [h]; have hnot : not (b.epoch_ms >= a.epoch_ms) := by omega; simp [hnot]
  . simp [h]; have hrev : b.epoch_ms >= a.epoch_ms := by omega; simp [hrev]

/- L4: Quality Invariant -/

def all_good_quality : ResultSet -> Prop :=
  List.Forall (fun dp => dp.qual = Quality.good)

theorem all_good_singleton (dp : DataPoint) :
    all_good_quality [dp] <-> dp.qual = Quality.good := by
  unfold all_good_quality
  simp

theorem all_good_of_good_quality (dp : DataPoint) (hq : dp.qual = Quality.good) :
    all_good_quality [dp] := by
  unfold all_good_quality
  simp [hq]

theorem all_good_nil : all_good_quality [] := by
  unfold all_good_quality; trivial

end HistorianSchema
