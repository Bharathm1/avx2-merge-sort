#include "merge_sort.h"
#include <iostream>
#include <stdlib.h>

masks global_masks;

__m256i load_reg256(int *a) {
  return _mm256_maskload_epi32(a, global_masks.load_store_mask);
}

__m256i load_reg256(int64_t *a) {
  return _mm256_maskload_epi64(a, global_masks.load_store_mask);
}

void store_reg256(int *a, __m256i& b) {
  _mm256_maskstore_epi32(a, global_masks.load_store_mask, b);
}

inline __m256i reverse(__m256i& v) {
  return _mm256_permutevar8x32_epi32(v, global_masks.rev_idx_mask);
}

inline __m256i interleave_low(__m256i& a, __m256i& b) {
  return _mm256_unpacklo_epi32(a,b);
}

inline __m256i interleave_high(__m256i& a, __m256i& b) {
  return _mm256_unpackhi_epi32(a,b);
}

inline void minmax(__m256i& a, __m256i& b, __m256i& minab, __m256i& maxab){
    minab = _mm256_min_epu32(a, b);
    maxab = _mm256_max_epu32(a, b);
}

inline void minmax(__m256i& a, __m256i& b){
  auto t = a;
  a = _mm256_min_epu32(a, b);
  b = _mm256_max_epu32(t, b);
  return;
}

inline void minmax64(__m256i& a, __m256i& b){
  auto mask = _mm256_cmpgt_epi32 (a, b);
  mask = _mm256_shuffle_epi32(mask, 0xA0);
  auto t = a;
  a = _mm256_blendv_epi8(a, b, mask);
  b = _mm256_blendv_epi8(b, t, mask);
}

inline void minmax64(__m256i& a, __m256i& b, __m256i& minab, __m256i& maxab){
  auto mask = _mm256_cmpgt_epi32 (a, b);
  mask = _mm256_shuffle_epi32(mask, 0xA0);
  minab = _mm256_blendv_epi8(a, b, mask);
  maxab = _mm256_blendv_epi8(b, a, mask);
}

inline void minmax_idx(__m256i& a, __m256i& b, __m256i& aidx, __m256i& bidx,
                       __m256i& minab, __m256i& maxab, __m256i& minidx, __m256i& maxidx) {
  auto mask = _mm256_cmpgt_epi32 (a, b);
  minab = _mm256_blendv_epi8(a, b, mask);
  maxab = _mm256_blendv_epi8(b, b, mask);

  minidx = _mm256_blendv_epi8(aidx, bidx, mask);
  maxidx = _mm256_blendv_epi8(bidx, bidx, mask);
}

inline __m256i shuffle(__m256i& a, int* idx_array) {
  __m256i idx = _mm256_load_si256((__m256i *)idx_array);
  return _mm256_permutevar8x32_epi32(a, idx);
}

inline __m256i register_shuffle(__m256i& a, __m256i& mask) {
  return _mm256_permutevar8x32_epi32(a, mask);
}


std::pair<__m256i, __m256i> bitonic_merge(__m256i& a, __m256i& b) {
  __m256i minabr, maxabr;
  __m256i br = reverse(b);
  minmax(a,br, minabr, maxabr);
  return std::make_pair(intra_register_sort(minabr), 
    intra_register_sort(maxabr));
}

inline void transpose8(__m256* row0, __m256* row1, __m256* row2, __m256* row3,
                       __m256* row4, __m256* row5, __m256* row6, __m256* row7) {
  __m256 __t0, __t1, __t2, __t3, __t4, __t5, __t6, __t7;
  __m256 __tt0, __tt1, __tt2, __tt3, __tt4, __tt5, __tt6, __tt7;
  __t0 = _mm256_unpacklo_ps(*row0, *row1);
  __t1 = _mm256_unpackhi_ps(*row0, *row1);
  __t2 = _mm256_unpacklo_ps(*row2, *row3);
  __t3 = _mm256_unpackhi_ps(*row2, *row3);
  __t4 = _mm256_unpacklo_ps(*row4, *row5);
  __t5 = _mm256_unpackhi_ps(*row4, *row5);
  __t6 = _mm256_unpacklo_ps(*row6, *row7);
  __t7 = _mm256_unpackhi_ps(*row6, *row7);
  __tt0 = _mm256_shuffle_ps(__t0,__t2,_MM_SHUFFLE(1,0,1,0));
  __tt1 = _mm256_shuffle_ps(__t0,__t2,_MM_SHUFFLE(3,2,3,2));
  __tt2 = _mm256_shuffle_ps(__t1,__t3,_MM_SHUFFLE(1,0,1,0));
  __tt3 = _mm256_shuffle_ps(__t1,__t3,_MM_SHUFFLE(3,2,3,2));
  __tt4 = _mm256_shuffle_ps(__t4,__t6,_MM_SHUFFLE(1,0,1,0));
  __tt5 = _mm256_shuffle_ps(__t4,__t6,_MM_SHUFFLE(3,2,3,2));
  __tt6 = _mm256_shuffle_ps(__t5,__t7,_MM_SHUFFLE(1,0,1,0));
  __tt7 = _mm256_shuffle_ps(__t5,__t7,_MM_SHUFFLE(3,2,3,2));
  *row0 = _mm256_permute2f128_ps(__tt0, __tt4, 0x20);
  *row1 = _mm256_permute2f128_ps(__tt1, __tt5, 0x20);
  *row2 = _mm256_permute2f128_ps(__tt2, __tt6, 0x20);
  *row3 = _mm256_permute2f128_ps(__tt3, __tt7, 0x20);
  *row4 = _mm256_permute2f128_ps(__tt0, __tt4, 0x31);
  *row5 = _mm256_permute2f128_ps(__tt1, __tt5, 0x31);
  *row6 = _mm256_permute2f128_ps(__tt2, __tt6, 0x31);
  *row7 = _mm256_permute2f128_ps(__tt3, __tt7, 0x31);
}

inline void transpose8_64i(__m256i& row0, __m256i& row1, __m256i& row2, __m256i& row3,
                           __m256i& row4, __m256i& row5, __m256i& row6, __m256i& row7) {
  __m256i __t0 = _mm256_unpacklo_epi64(row0, row1);
  __m256i __t1 = _mm256_unpackhi_epi64(row0, row1);
  __m256i __t2 = _mm256_unpacklo_epi64(row2, row3);
  __m256i __t3 = _mm256_unpackhi_epi64(row2, row3);

  __m256i __t4 = _mm256_unpacklo_epi64(row4, row5);
  __m256i __t5 = _mm256_unpackhi_epi64(row4, row5);
  __m256i __t6 = _mm256_unpacklo_epi64(row6, row7);
  __m256i __t7 = _mm256_unpackhi_epi64(row6, row7);

  row0 = (__m256i) _mm256_permute2f128_si256(__t0, __t2, 0x20);
  row2 = (__m256i) _mm256_permute2f128_si256(__t1, __t3, 0x20);
  row4 = (__m256i) _mm256_permute2f128_si256(__t0, __t2, 0x31);
  row6 = (__m256i) _mm256_permute2f128_si256(__t1, __t3, 0x31);

  row1 = (__m256i) _mm256_permute2f128_si256(__t4, __t6, 0x20);
  row3 = (__m256i) _mm256_permute2f128_si256(__t5, __t7, 0x20);
  row5 = (__m256i) _mm256_permute2f128_si256(__t4, __t6, 0x31);
  row7 = (__m256i) _mm256_permute2f128_si256(__t5, __t7, 0x31);


}

inline void transpose4_64i(__m256i& row0, __m256i& row1, __m256i& row2, __m256i& row3) {
  __m256i __t0 = _mm256_unpacklo_epi64(row0, row1);
  __m256i __t1 = _mm256_unpackhi_epi64(row0, row1);
  __m256i __t2 = _mm256_unpacklo_epi64(row2, row3);
  __m256i __t3 = _mm256_unpackhi_epi64(row2, row3);

  row0 = (__m256i) _mm256_permute2f128_si256(__t0, __t2, 0x20);
  row1 = (__m256i) _mm256_permute2f128_si256(__t1, __t3, 0x20);
  row2 = (__m256i) _mm256_permute2f128_si256(__t0, __t2, 0x31);
  row3 = (__m256i) _mm256_permute2f128_si256(__t1, __t3, 0x31);
}

void sort_columns_64i(__m256i& row0, __m256i& row1, __m256i& row2, __m256i& row3) {
  minmax64(row0,row1);
  minmax64(row2,row3);
  minmax64(row0,row2);
  minmax64(row1,row3);
  minmax64(row1,row2);
}

void merge8_64i(__m256i& row0, __m256i& row1, __m256i& row2, __m256i& row3,
                __m256i& row4, __m256i& row5, __m256i& row6, __m256i& row7) {
  minmax64(row0, row4);
  minmax64(row1, row5);
  minmax64(row2, row6);
  minmax64(row3, row7);

  minmax64(row2, row4);
  minmax64(row3, row5);

  minmax64(row1, row2);
  minmax64(row3, row4);
  minmax64(row5, row6);
}


void sort_columns(__m256i& row0, __m256i& row1, __m256i& row2, __m256i& row3,
                  __m256i& row4, __m256i& row5, __m256i& row6, __m256i& row7) {

    minmax(row0,row1);
    minmax(row2,row3);
    minmax(row4,row5);
    minmax(row6,row7);

    minmax(row0,row2);
    minmax(row1,row3);
    minmax(row4,row6);
    minmax(row5,row7);

    minmax(row1,row2);
    minmax(row0,row4);
    minmax(row5,row6);
    minmax(row3,row7);

    minmax(row1,row5);
    minmax(row2,row6);

    minmax(row1,row4);
    minmax(row3,row6);

    minmax(row2,row4);
    minmax(row3,row5);

    minmax(row3,row4);
}

void sort64(__m256i& row0, __m256i& row1, __m256i& row2, __m256i& row3,
            __m256i& row4, __m256i& row5, __m256i& row6, __m256i& row7) {
  sort_columns(row0, row1, row2, row3, row4, row5, row6, row7);
  transpose8((__m256 *)&row0, (__m256 *)&row1, (__m256 *)&row2, (__m256 *)&row3,
             (__m256 *)&row4, (__m256 *)&row5, (__m256 *)&row6, (__m256 *)&row7);
}

void sort64(__m256i* row) {
  sort64(row[0], row[1], row[2], row[3], row[4], row[5], row[6], row[7]);
}


void sort16_64i(__m256i& row0, __m256i& row1, __m256i& row2, __m256i& row3) {
  sort_columns_64i(row0, row1, row2, row3);
  transpose4_64i(row0, row1, row2, row3);
}

void sort32_64i(__m256i& row0, __m256i& row1, __m256i& row2, __m256i& row3,
            __m256i& row4, __m256i& row5, __m256i& row6, __m256i& row7) {
  sort_columns_64i(row0, row1, row2, row3);
  sort_columns_64i(row4, row5, row6, row7);

  merge8_64i(row0, row1, row2, row3, row4, row5, row6, row7);
  transpose8_64i(row0, row1, row2, row3, row4, row5, row6, row7);
}


__m256i intra_register_sort(__m256i& l8) {
  __m256i min, max;
  // phase 1
  auto l8_1 = register_shuffle(l8, global_masks.swap_128);
  minmax(l8, l8_1, min, max);
  auto l4 = _mm256_permute2x128_si256(min, max, 0x20);
  // phase 2
  auto l4_1 = _mm256_shuffle_epi32(l4, 0x4e);
  minmax(l4, l4_1, min, max);
  auto l2 = _mm256_unpacklo_epi64(min, max);
  // phase 3
  auto l2_1 = _mm256_shuffle_epi32(l2, 0xb1);
  minmax(l2, l2_1, min, max);
  min = _mm256_shuffle_epi32(min, 0xd8);
  max = _mm256_shuffle_epi32(max, 0xd8);
  return _mm256_unpacklo_epi32(min, max);
}

void initialize() {
  // directly set load store mask in 32B aligned memory
  alignas(32) int load_store_mask[8] =
    {1<<31,1<<31,1<<31,1<<31,1<<31,1<<31,1<<31,1<<31};
  global_masks.load_store_mask = 
      _mm256_load_si256((__m256i *) &load_store_mask[0]);

  // load the remaining masks
  int rev_idx_mask[8] = {7,6,5,4,3,2,1,0};
  int swap_128[8] = {4,5,6,7,0,1,2,3};
  global_masks.rev_idx_mask = load_reg256(&rev_idx_mask[0]);
  global_masks.swap_128 = load_reg256(&swap_128[0]);
}

void print_test_array(__m256i res, const std::string& msg) {
  std::cout << msg << std::endl;
  for (int i=0; i<8; i++)
    std::cout << ((int *)&res)[i] << "\t";
  std::cout << std::endl;
}


void test_minmax64() {
  int64_t test_arr1[4] = {11,((int64_t)10<<32)|2,((int64_t)11<<32)|3,4};
  int64_t test_arr2[4] = {((int64_t)110<<32)|1,12,13,((int64_t)14<<32)|14};
  __m256i r1, r2, r3, r4;
  __m256i test1 = load_reg256((int *)&test_arr1[0]);
  __m256i test2 = load_reg256((int *)&test_arr2[0]);
  __m256i test3 = load_reg256((int *)&test_arr1[0]);
  __m256i test4 = load_reg256((int *)&test_arr2[0]);
  for(int i =0; i < 10000000; i++){
    minmax64(test1, test2, r1, r2);
    minmax64(test3, test4, r3, r4);
    minmax64(test1, test2, r1, r2);
    minmax64(test3, test4, r3, r4);
    minmax64(test1, test2, r1, r2);
    minmax64(test3, test4, r3, r4);
    minmax64(test1, test2, r1, r2);
    minmax64(test3, test4, r3, r4);

    minmax64(test1, test2, r1, r2);
    minmax64(test3, test4, r3, r4);
    minmax64(test1, test2, r1, r2);
    minmax64(test3, test4, r3, r4);
    minmax64(test1, test2, r1, r2);
    minmax64(test3, test4, r3, r4);
    minmax64(test1, test2, r1, r2);
    minmax64(test3, test4, r3, r4);

    minmax64(test1, test2, r1, r2);
    minmax64(test3, test4, r3, r4);
    minmax64(test1, test2, r1, r2);
    minmax64(test3, test4, r3, r4);
    minmax64(test1, test2, r1, r2);
    minmax64(test3, test4, r3, r4);
    minmax64(test1, test2, r1, r2);
    minmax64(test3, test4, r3, r4);

    minmax64(test1, test2, r1, r2);
    minmax64(test3, test4, r3, r4);
    minmax64(test1, test2, r1, r2);
    minmax64(test3, test4, r3, r4);
    minmax64(test1, test2, r1, r2);
    minmax64(test3, test4, r3, r4);
    minmax64(test1, test2, r1, r2);
    minmax64(test3, test4, r3, r4);
  }
//  print_test_array(r1,"min64");
//  print_test_array(r2,"max64");
}

void test_minmax() {
  int64_t test_arr1[4] = {11,((int64_t)10<<32)|2,((int64_t)11<<32)|3,4};
  int64_t test_arr2[4] = {((int64_t)110<<32)|1,12,13,((int64_t)14<<32)|14};
  __m256i r1, r2;
  __m256i test1 = load_reg256((int *)&test_arr1[0]);
  __m256i test2 = load_reg256((int *)&test_arr2[0]);
  for(int i =0; i < 10000000; i++){
    minmax(test1, test2, r1, r2);
    minmax(test1, test2, r1, r2);
    minmax(test1, test2, r1, r2);
    minmax(test1, test2, r1, r2);

    minmax(test1, test2, r1, r2);
    minmax(test1, test2, r1, r2);
    minmax(test1, test2, r1, r2);
    minmax(test1, test2, r1, r2);

    minmax(test1, test2, r1, r2);
    minmax(test1, test2, r1, r2);
    minmax(test1, test2, r1, r2);
    minmax(test1, test2, r1, r2);

    minmax(test1, test2, r1, r2);
    minmax(test1, test2, r1, r2);
    minmax(test1, test2, r1, r2);
    minmax(test1, test2, r1, r2);
  }

//  print_test_array(r1,"min");
//  print_test_array(r2,"max");
}

void test_minmax_idx() {
  int64_t test_arr1[4] = {11,((int64_t)10<<32)|2,((int64_t)11<<32)|3,4};
  int64_t test_arr2[4] = {((int64_t)110<<32)|1,12,13,((int64_t)14<<32)|14};

  __m256i r1, r2, r3, r4;
  __m256i test1 = load_reg256((int *)&test_arr1[0]);
  __m256i test2 = load_reg256((int *)&test_arr2[0]);
  __m256i test3 = load_reg256((int *)&test_arr1[0]);
  __m256i test4 = load_reg256((int *)&test_arr2[0]);
  for(int i =0; i < 10000000; i++){
    minmax_idx(test1, test2, test3, test4, r1, r2, r3, r4);
    minmax_idx(test1, test2, test3, test4, r1, r2, r3, r4);
    minmax_idx(test1, test2, test3, test4, r1, r2, r3, r4);
    minmax_idx(test1, test2, test3, test4, r1, r2, r3, r4);
    minmax_idx(test1, test2, test3, test4, r1, r2, r3, r4);
    minmax_idx(test1, test2, test3, test4, r1, r2, r3, r4);
    minmax_idx(test1, test2, test3, test4, r1, r2, r3, r4);
    minmax_idx(test1, test2, test3, test4, r1, r2, r3, r4);
    minmax_idx(test1, test2, test3, test4, r1, r2, r3, r4);
    minmax_idx(test1, test2, test3, test4, r1, r2, r3, r4);
    minmax_idx(test1, test2, test3, test4, r1, r2, r3, r4);
    minmax_idx(test1, test2, test3, test4, r1, r2, r3, r4);
    minmax_idx(test1, test2, test3, test4, r1, r2, r3, r4);
    minmax_idx(test1, test2, test3, test4, r1, r2, r3, r4);
    minmax_idx(test1, test2, test3, test4, r1, r2, r3, r4);
    minmax_idx(test1, test2, test3, test4, r1, r2, r3, r4);
  }

//  print_test_array(r1,"min");
//  print_test_array(r2,"max");
}


void test_basic() {
  __m256i min, max;
  int test_arr1[8] = {1,2,3,4,15,16,17,18};
  int test_arr2[8] = {11,12,13,14,5,6,7,8};
  int idx[8] = {4,5,6,7,0,1,2,3};
  __m256i test1 = load_reg256(&test_arr1[0]);
  __m256i test2 = load_reg256(&test_arr2[0]);
  __m256i mask = load_reg256(&idx[0]);
  print_test_array(test1, "test1");
  print_test_array(test2, "test2");
  print_test_array(reverse(test1), "Reverse Output");
  print_test_array(register_shuffle(test1, mask), "Register shuffle");
  print_test_array(interleave_low(test1, test2), "interleave_low");
  print_test_array(interleave_high(test1, test2), "interleave_high");
  minmax(test1, test2, min, max);
  print_test_array(min, "Minimum");
  print_test_array(max, "Maximum");
}
