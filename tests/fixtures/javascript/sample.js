/** @param value input */
function outer(value) {
  const object = {method() { return `hello ${value + 1}`; }};
  // regex versus division
  return /hello\w+/gi.test(object.method()) ? value / 2 : null;
}
