// Seconds left in a countdown Orbit reports every few seconds, counted every second in
// between. `endsAt` is when the countdown reaches 0 (Date.now() clock); never more than
// Orbit's own last number.
export function secondsLeft(reported: number, endsAt: number, now: number): number {
  if (!(reported > 0) || !(endsAt > 0)) return 0;
  return Math.max(0, Math.min(reported, Math.ceil((endsAt - now) / 1000)));
}
