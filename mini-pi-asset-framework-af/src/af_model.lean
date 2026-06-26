-- AF Model: PI Asset Framework formalization in Lean 4
-- Core types and theorems about asset hierarchies

structure AFElement where
  name : String
  elemType : Nat
  parent : Option AFElement
  children : List AFElement
  attributes : List (String * Nat)

structure AFAttrTemplate where
  name : String
  valueType : Nat
  hasDefault : Bool
  defaultValue : Option Nat

structure AFTemplate where
  name : String
  baseTemplate : Option AFTemplate
  attrTemplates : List AFAttrTemplate

def isAncestor (a e : AFElement) : Bool :=
  match e.parent with
  | none => false
  | some p => p == a || isAncestor a p

theorem noSelfAncestor (e : AFElement) : isAncestor e e = false := by
  simp [isAncestor]

theorem rootHasNoAncestor (e : AFElement) (h : e.parent = none) :
  forall a, isAncestor a e = false := by
  intro a
  simp [isAncestor, h]

def inheritsFrom (t base : AFTemplate) : Bool :=
  match t.baseTemplate with
  | none => false
  | some b => b == base || inheritsFrom b base

def elementPath (e : AFElement) : List String :=
  match e.parent with
  | none => [e.name]
  | some p => elementPath p ++ [e.name]

theorem pathNotEmpty (e : AFElement) : elementPath e != [] := by
  unfold elementPath
  cases e.parent with
  | none => simp
  | some p => simp

def subtreeSize (e : AFElement) : Nat :=
  1 + (e.children.map subtreeSize).sum

theorem subtreeSizePositive (e : AFElement) : subtreeSize e >= 1 := by
  unfold subtreeSize
  omega

def searchElements (elems : List AFElement) (name : String) :
  List AFElement :=
  elems.filter (fun e => e.name == name)

theorem searchResultSubset (elems : List AFElement) (name : String) :
  forall e, e in searchElements elems name -> e in elems := by
  intro e h
  unfold searchElements at h
  have := List.mem_filter.mp h
  exact this.left

inductive TriggerOp where
  | GT | GE | LT | LE | EQ | NEQ

def evalTrigger (op : TriggerOp) (value threshold : Float) : Bool :=
  match op with
  | TriggerOp.GT => value > threshold
  | TriggerOp.GE => value >= threshold
  | TriggerOp.LT => value < threshold
  | TriggerOp.LE => value <= threshold
  | TriggerOp.EQ => value == threshold
  | TriggerOp.NEQ => value != threshold

theorem triggerDeterministic (op : TriggerOp) (v t : Float) :
  evalTrigger op v t = evalTrigger op v t := rfl

inductive BatchState where
  | Idle | Running | Held | Complete | Aborted
  deriving BEq, Inhabited

def batchTransition (s : BatchState) (cmd : String) : BatchState :=
  match s, cmd with
  | BatchState.Idle, "START" => BatchState.Running
  | BatchState.Running, "HOLD" => BatchState.Held
  | BatchState.Held, "RESUME" => BatchState.Running
  | BatchState.Running, "COMPLETE" => BatchState.Complete
  | _, "ABORT" => BatchState.Aborted
  | _, _ => s

theorem idleStaysIdle (s : BatchState) (cmd : String)
  (h : s = BatchState.Idle) (hcmd : cmd != "START") :
  batchTransition s cmd = BatchState.Idle := by
  subst h
  unfold batchTransition
  split <;> simp [hcmd]
