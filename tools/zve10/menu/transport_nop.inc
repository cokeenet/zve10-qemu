/* transport_nop.inc —— 空实现。类型在 transport.h 里。 */
#ifndef NE_TRANSPORT_NOP_INC
#define NE_TRANSPORT_NOP_INC

static void ne_transport_cache(const void *c, unsigned n, uint64_t f)
{ (void)c; (void)n; (void)f; }
static void ne_transport_retired(uint64_t f) { (void)f; }
static void ne_transport_ui(const struct ne_observation *o, uint64_t f)
{ (void)o; (void)f; }
static void ne_transport_start(void) { }

#endif
