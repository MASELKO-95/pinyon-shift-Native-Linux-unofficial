#pragma once

// Guest-code observers for FH1's frame boundary and title draw emitter,
// called from the mid-asm hooks in config/rexglue/analysis. They feed the
// critical-path trace and the source-frame counter. The GPU fence wait hook
// blocks the title's fence polling until the command processor writes.
