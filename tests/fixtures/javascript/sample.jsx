function View({name}) {
  return <section className="card"><b>{`Hello ${name}`}</b>{items.map(x => <Item key={x.id} />)}</section>;
}
