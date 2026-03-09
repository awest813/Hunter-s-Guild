
## 2025-03-09 - [Optimize size() evaluation in ChoiceSearch loop]
**Learning:** `parray` lacks standard iterator begin/end support causing `const auto& entry : this->entries` to fail compilation. To optimize array-based loop sizes we instead cache the `.size()` result into a local variable before the loop and use an index loop.
**Action:** When iterating over `parray` structures in performance-sensitive code, manually cache the `.size()` method result outside the loop.
