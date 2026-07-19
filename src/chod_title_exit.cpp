#include <rex/hook.h>
#include <rex/logging.h>
#include <rex/system/kernel_state.h>

REX_EXTERN(__imp__sub_826395F8);

REX_HOOK_RAW(sub_826395F8) {
  if (ctx.r3.u32 == 0) {
    REXLOG_INFO("HoD title exit: dashboard launch request mapped to app quit");
    rex::system::kernel_state()->TerminateTitle();
    return;
  }

  __imp__sub_826395F8(ctx, base);
}
