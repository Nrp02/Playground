'use strict';

const { GraphDB } = require('./query.js');

function randomChoice(arr) {
  return arr[Math.floor(Math.random() * arr.length)];
}

function buildLargeGraph() {
  const db = new GraphDB();
  const firstNames = ['Alex', 'Sam', 'Jordan', 'Taylor', 'Morgan', 'Casey', 'Riley', 'Drew'];
  const lastNames = ['Lee', 'Park', 'Kim', 'Chen', 'Diaz', 'Singh', 'Novak', 'Reyes'];
  const companyNames = ['Acme', 'Globex', 'Initech', 'Umbrella', 'Soylent', 'Hooli', 'Stark', 'Wayne'];
  const cityNames = ['Springfield', 'Metropolis', 'Gotham', 'Star City', 'Central City'];

  const personIds = [];
  const companyIds = [];
  const cityIds = [];

  for (let i = 0; i < cityNames.length; i += 1) {
    cityIds.push(db.addNode({ labels: ['City'], properties: { name: cityNames[i] } }).id);
  }
  for (let i = 0; i < companyNames.length; i += 1) {
    companyIds.push(db.addNode({ labels: ['Company'], properties: { name: companyNames[i] } }).id);
  }
  for (let i = 0; i < 3000; i += 1) {
    const node = db.addNode({
      labels: ['Person'],
      properties: {
        name: `${randomChoice(firstNames)} ${randomChoice(lastNames)}`,
        age: 20 + Math.floor(Math.random() * 45),
      },
    });
    personIds.push(node.id);
  }

  for (const personId of personIds) {
    const friendCount = 2 + Math.floor(Math.random() * 4);
    for (let i = 0; i < friendCount; i += 1) {
      const other = randomChoice(personIds);
      if (other !== personId) db.addEdge({ type: 'KNOWS', from: personId, to: other });
    }
    db.addEdge({ type: 'WORKS_AT', from: personId, to: randomChoice(companyIds) });
    db.addEdge({ type: 'LIVES_IN', from: personId, to: randomChoice(cityIds) });
  }

  return { db, personIds, companyIds, cityIds };
}

function main() {
  const { db, personIds, cityIds } = buildLargeGraph();
  console.log(`Built graph with ${db.graph.nodes.size} nodes and ${db.graph.edges.size} edges.\n`);

  const targetPerson = personIds[0];
  const targetCity = cityIds[0];
  console.log(`Query: companies where friends-of-friends of ${targetPerson} work, in city ${targetCity}`);

  const cityMembers = new Set(db.neighbors(targetCity, { direction: 'in', edgeType: 'LIVES_IN', depth: 1 }).map((n) => n.id));

  function runQuery() {
    const fof = db
      .match({ label: 'Person' })
      .where((node) => node.id === targetPerson)
      .out('KNOWS')
      .out('KNOWS')
      .run();
    const companySet = new Map();
    for (const person of fof) {
      const companies = db.neighbors(person.id, { direction: 'out', edgeType: 'WORKS_AT', depth: 1 });
      for (const company of companies) {
        const worksInTargetCity = db
          .neighbors(company.id, { direction: 'in', edgeType: 'WORKS_AT', depth: 1 })
          .some((worker) => cityMembers.has(worker.id));
        if (worksInTargetCity) companySet.set(company.id, company);
      }
    }
    return Array.from(companySet.values());
  }

  db.createIndex('Person', 'age');
  const withIndexExplain = db.match({ label: 'Person', where: { age: db.getNode(targetPerson).properties.age } }).explain();
  const results = runQuery();
  console.log(`Found ${results.length} candidate companies:`, results.map((c) => c.properties.name));
  console.log('Plan for the age-indexed lookup used above:');
  console.log(JSON.stringify(withIndexExplain.plan, null, 2));
  console.log(`estimated rows: ${withIndexExplain.estimatedRows}, actual scanned: ${withIndexExplain.actualRowsScanned}`);

  console.log('\nDropping the age index and re-running the same match...');
  db.dropIndex('Person', 'age');
  const withoutIndexExplain = db.match({ label: 'Person', where: { age: db.getNode(targetPerson).properties.age } }).explain();
  console.log(JSON.stringify(withoutIndexExplain.plan, null, 2));
  console.log(`estimated rows: ${withoutIndexExplain.estimatedRows}, actual scanned: ${withoutIndexExplain.actualRowsScanned}`);
  console.log(
    `Scan size grew from ${withIndexExplain.actualRowsScanned} (index) to ${withoutIndexExplain.actualRowsScanned} (full scan).`
  );

  console.log('\nShortest path between two random people:');
  const a = personIds[10];
  const b = personIds[2000];
  const path = db.shortestPath(a, b, { edgeTypes: ['KNOWS'], maxDepth: 6 });
  console.log(path ? `path length ${path.length}: ${path.path.join(' -> ')}` : 'unreachable within maxDepth');

  console.log('\nTransaction rollback demo:');
  const beforeCount = db.graph.nodes.size;
  try {
    db.transaction(() => {
      db.addNode({ labels: ['Person'], properties: { name: 'Ghost', age: 1 } });
      db.addNode({ labels: ['Person'], properties: { name: 'Ghost2', age: 1 } });
      throw new Error('simulated failure mid-batch');
    });
  } catch (err) {
    console.log(`caught expected error: ${err.message}`);
  }
  console.log(`node count before: ${beforeCount}, after rollback: ${db.graph.nodes.size}`);
}

main();
