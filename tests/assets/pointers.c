// This file is used to test the debug information
// generated to describe pointer variables.

int deref_add(int *a, int *b) {
  int _a = *a;
  int _b = *b;
  return _a + _b;
}

void ptr_inc(int *inc) {
  *inc += 1;
}

int main(void) {
  int main_a = 9;
  int main_b = 18;
  int main_sum = deref_add(&main_a, &main_b);

  // Here the value of `main_sum` is increased by 1.
  ptr_inc(&main_sum);
  return 0;
}

