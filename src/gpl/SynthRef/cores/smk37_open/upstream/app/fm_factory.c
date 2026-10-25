/* SPDX-License-Identifier: GPL-3.0-only WITH GPL-3.0-linking-exception */
#include "fm_patch.h"

/* Immutable Flash data, shared by firmware and WASM. Never seed user Flash:
 * saved banks take precedence, and existing projects carry their own patches. */
#include "factory_dx7_packed.inc"
FM1_DSP_CODE void groove_fm_factory(uint8_t patch[156], unsigned program)
{
    uint8_t raw[128], stack[FACTORY_STACK];
    unsigned voice=program%128U, out=0;
    for(unsigned i=factory_offsets[voice];i<factory_offsets[voice+1];++i) {
        unsigned depth=0;stack[depth++]=factory_tokens[i];
        do {
            unsigned token=stack[--depth];
            if(token<128) {
                unsigned field=out<102 ? (out%6U)*17U+out/6U : out;
                raw[field]=(uint8_t)token;++out;
            }
            else {
                unsigned pair=(token-128)*2;
                stack[depth++]=factory_pairs[pair+1];
                stack[depth++]=factory_pairs[pair];
            }
        } while(depth);
    }
    groove_fm_unpack(patch,raw);
}
