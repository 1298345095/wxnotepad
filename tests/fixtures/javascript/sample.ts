interface Result<T> { value: T; }
type Choice = string | number;
class Box<T> {
  readonly value: T;
  method(input: Choice): Result<T> {
    return {value: this.value};
  }
}
