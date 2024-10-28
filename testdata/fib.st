language core;

extend with
  #tuples,
  #arithmetic-operators,
  #natural-literals;

fn helper(p : {Nat, Nat, Nat}) -> {Nat, Nat, Nat} {
  return Nat::rec(p.1, p, fn (i : Nat) {
    return fn(r : {Nat, Nat, Nat}) {
      return {r.1 - 1, r.3, r.2 + r.3}
    }
  })
}

fn fib(n : Nat) -> Nat {
  return helper({n,0,1}).2
}

fn main(n : Nat) -> Nat {
  return fib(n)
}
