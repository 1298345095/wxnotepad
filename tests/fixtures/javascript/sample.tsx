interface Props { name: string; }
function View(props: Props): JSX.Element {
  const value: string = `Hello ${props.name}`;
  return <section title={value}><b>{value}</b></section>;
}
