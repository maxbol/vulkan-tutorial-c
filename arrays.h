#ifndef ARRAYS_H
#define ARRAYS_H

#define da_append(xs, x)                                                       \
  do {                                                                         \
    if (xs.count >= xs.capacity) {                                             \
      if (xs.capacity == 0)                                                    \
        xs.capacity = 256;                                                     \
      xs.capacity = xs.capacity * 2;                                           \
      xs.items = realloc(xs.items, xs.capacity * sizeof(*xs.items));           \
    }                                                                          \
    xs.items[xs.count++] = x;                                                  \
  } while (0)

#define da_capacity(xs, x)                                                     \
  do {                                                                         \
    xs.capacity = x;                                                           \
    xs.items = realloc(xs.items, xs.capacity * sizeof(*xs.items));             \
  } while (0)

#define da_replace(xs, i, x)                                                   \
  do {                                                                         \
    if (i >= xs.capacity) {                                                    \
      xs.capacity = (i + 1) * 2;                                               \
      xs.items = realloc(xs.items, xs.capacity * sizeof(*xs.items));           \
    }                                                                          \
    if (i >= xs.count) {                                                       \
      xs.count = i + 1;                                                        \
    }                                                                          \
    xs.items[i] = x;                                                           \
  } while (0)

#define da_free(xs)                                                            \
  do {                                                                         \
    free(xs.items);                                                            \
    xs.items = NULL;                                                           \
    xs.count = 0;                                                              \
  } while (0)

#define da_empty(xs)                                                           \
  do {                                                                         \
    da_free(xs);                                                               \
    xs.items = malloc(xs.capacity * sizeof(*xs.items));                        \
  } while (0)

#define da_swap(xs, i, j)                                                      \
  do {                                                                         \
    typeof(xs.items[i]) tmp = xs.items[i];                                     \
    xs.items[i] = xs.items[j];                                                 \
    xs.items[j] = tmp;                                                         \
  } while (0)

#define da_clone(xs)                                                           \
  ({                                                                           \
    typeof(xs) ys = {0};                                                       \
    ys.capacity = xs.capacity;                                                 \
    ys.count = xs.count;                                                       \
    ys.items = malloc(ys.capacity * sizeof(*ys.items));                        \
    memcpy(ys.items, xs.items, xs.count * sizeof(*xs.items));                  \
    ys;                                                                        \
  })

#define da_quicksort(xs, cmp)                                                  \
  do {                                                                         \
    if (xs.count <= 1)                                                         \
      break;                                                                   \
    int stack[128];                                                            \
    int sp = 0;                                                                \
    stack[sp++] = 0;                                                           \
    stack[sp++] = xs.count - 1;                                                \
    while (sp > 0) {                                                           \
      int end = stack[--sp];                                                   \
      int start = stack[--sp];                                                 \
      if (start >= end)                                                        \
        continue;                                                              \
      int pivot = start + (end - start) / 2;                                   \
      int left = start;                                                        \
      int right = end;                                                         \
      while (left <= right) {                                                  \
        while (cmp(xs.items[left], xs.items[pivot]) < 0)                       \
          left++;                                                              \
        while (cmp(xs.items[right], xs.items[pivot]) > 0)                      \
          right--;                                                             \
        if (left <= right) {                                                   \
          da_swap(xs, left, right);                                            \
          left++;                                                              \
          right--;                                                             \
        }                                                                      \
      }                                                                        \
      if (start < right) {                                                     \
        stack[sp++] = start;                                                   \
        stack[sp++] = right;                                                   \
      }                                                                        \
      if (left < end) {                                                        \
        stack[sp++] = left;                                                    \
        stack[sp++] = end;                                                     \
      }                                                                        \
    }                                                                          \
  } while (0)

#endif /* ARRAYS_H */
