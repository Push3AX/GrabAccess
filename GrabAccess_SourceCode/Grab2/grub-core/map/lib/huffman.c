/*
 * Copyright (C) 2014 Michael Brown <mbrown@fensystems.co.uk>.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of the
 * License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA
 * 02110-1301, USA.
 */

/**
 * @file
 *
 * Huffman alphabets
 *
 */

#include <stdint.h>
#include <string.h>
#include <stdio.h>
#include <assert.h>
#include <huffman.h>

/**
 * Construct Huffman alphabet
 *
 * @v alphabet    Huffman alphabet
 * @v lengths    Symbol length table
 * @v count    Number of symbols
 * @ret rc    Return status code
 */
int huffman_alphabet ( struct huffman_alphabet *alphabet,
           uint8_t *lengths, unsigned int count ) {
  struct huffman_symbols *sym;
  unsigned int huf;
  unsigned int cum_freq;
  unsigned int bits;
  unsigned int raw;
  unsigned int adjustment;
  unsigned int prefix;
  int empty;
  int complete;

  /* Clear symbol table */
  memset ( alphabet->huf, 0, sizeof ( alphabet->huf ) );

  /* Count number of symbols with each Huffman-coded length */
  empty = 1;
  for ( raw = 0 ; raw < count ; raw++ ) {
    bits = lengths[raw];
    if ( bits ) {
      alphabet->huf[ bits - 1 ].freq++;
      empty = 0;
    }
  }

  /* In the degenerate case of having no symbols (i.e. an unused
   * alphabet), generate a trivial alphabet with exactly two
   * single-bit codes.  This allows callers to avoid having to
   * check for this special case.
   */
  if ( empty )
    alphabet->huf[0].freq = 2;

  /* Populate Huffman-coded symbol table */
  huf = 0;
  cum_freq = 0;
  for ( bits = 1 ; bits <= ( sizeof ( alphabet->huf ) /
           sizeof ( alphabet->huf[0] ) ) ; bits++ ) {
    sym = &alphabet->huf[ bits - 1 ];
    sym->bits = bits;
    sym->shift = ( HUFFMAN_BITS - bits );
    sym->start = ( huf << sym->shift );
    sym->raw = &alphabet->raw[cum_freq];
    huf += sym->freq;
    if ( huf > ( 1U << bits ) ) {
      printf ( "Huffman alphabet has too many symbols with "
            "lengths <=%d\n", bits );
      return -1;
    }
    huf <<= 1;
    cum_freq += sym->freq;
  }
  complete = ( huf == ( 1U << bits ) );

  /* Populate raw symbol table */
  for ( raw = 0 ; raw < count ; raw++ ) {
    bits = lengths[raw];
    if ( bits ) {
      sym = &alphabet->huf[ bits - 1 ];
      *(sym->raw++) = raw;
    }
  }

  /* Adjust Huffman-coded symbol table raw pointers and populate
   * quick lookup table.
   */
  for ( bits = 1 ; bits <= ( sizeof ( alphabet->huf ) /
           sizeof ( alphabet->huf[0] ) ) ; bits++ ) {
    sym = &alphabet->huf[ bits - 1 ];

    /* Adjust raw pointer */
    sym->raw -= sym->freq; /* Reset to first symbol */
    adjustment = ( sym->start >> sym->shift );
    sym->raw -= adjustment; /* Adjust for quick indexing */

    /* Populate quick lookup table */
    for ( prefix = ( sym->start >> HUFFMAN_QL_SHIFT ) ;
          prefix < ( 1 << HUFFMAN_QL_BITS ) ; prefix++ ) {
      alphabet->lookup[prefix] = ( bits - 1 );
    }
  }

  /* Check that there are no invalid codes */
  if ( ! complete ) {
    printf ( "Huffman alphabet is incomplete\n" );
    return -1;
  }

  return 0;
}

/**
 * Get Huffman symbol set
 *
 * @v alphabet    Huffman alphabet
 * @v huf    Raw input value (normalised to HUFFMAN_BITS bits)
 * @ret sym    Huffman symbol set
 */
struct huffman_symbols * huffman_sym ( struct huffman_alphabet *alphabet,
               unsigned int huf ) {
  struct huffman_symbols *sym;
  unsigned int lookup_index;

  /* Find symbol set for this length */
  lookup_index = ( huf >> HUFFMAN_QL_SHIFT );
  sym = &alphabet->huf[ alphabet->lookup[ lookup_index ] ];
  while ( huf < sym->start )
    sym--;
  return sym;
}
