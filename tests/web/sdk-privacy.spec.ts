import { readFileSync, realpathSync, existsSync } from "node:fs";
import path from "node:path";
import ts from "typescript";

const web = path.resolve(__dirname, "../../web");
const client = "@zmkfirmware/zmk-studio-ts-client";

// Follow package lookup from both importers. A future nested SDK must be checked
// independently instead of assuming that the hook uses the application's SDK.
function packageRoot(from: string, name: string): string {
  for (let at = from; ; at = path.dirname(at)) {
    const candidate = path.join(at, "node_modules", name);
    if (existsSync(path.join(candidate, "package.json")))
      return realpathSync(candidate);
    if (path.dirname(at) === at) throw new Error(`Missing installed ${name}`);
  }
}

const hook = packageRoot(web, "@cormoran/zmk-studio-react-hook");
const importers = [
  ["application", web],
  ["React hook", hook],
];

function inspectCodec(source: string) {
  const file = ts.createSourceFile(
    "installed-sdk.js",
    source,
    ts.ScriptTarget.Latest,
    true,
    ts.ScriptKind.JS,
  );
  const bodies: ts.Node[] = [];
  const codecs = new Set<string>();
  const walk = (node: ts.Node) => {
    if (
      ts.isMethodDeclaration(node) &&
      node.name.getText(file) === "transform" &&
      node.body
    ) {
      const found = new Set<string>();
      const scan = (child: ts.Node) => {
        if (
          ts.isCallExpression(child) &&
          ts.isPropertyAccessExpression(child.expression)
        ) {
          const target = child.expression.getText(file);
          if (target === "Request.encode" || target === "Response.decode")
            found.add(target);
        }
        ts.forEachChild(child, scan);
      };
      scan(node.body);
      if (found.size) {
        bodies.push(node.body);
        found.forEach((codec) => codecs.add(codec));
      }
    }
    ts.forEachChild(node, walk);
  };
  walk(file);
  if (bodies.length !== 2 || codecs.size !== 2)
    throw new Error(
      "Expected both installed request/response codec boundaries",
    );
  for (const body of bodies) {
    const rejectConsole = (node: ts.Node) => {
      if (ts.isIdentifier(node) && node.text === "console")
        throw new Error("Console reference in RPC payload codec");
      ts.forEachChild(node, rejectConsole);
    };
    rejectConsole(body);
  }
}

describe.each(importers)("%s SDK privacy boundary", (_label, importer) => {
  const root = packageRoot(importer, client);
  const pkg = JSON.parse(readFileSync(path.join(root, "package.json"), "utf8"));
  const entry = path.join(root, pkg.exports["."].import);
  const source = readFileSync(entry, "utf8");

  it("checks the shipped encoder and decoder without payload console references", () => {
    expect(() => inspectCodec(source)).not.toThrow();
  });

  it.each([
    ["request logging", "controller.enqueue(bytes)", "console.log(chunk)"],
    [
      "response logging",
      "controller.enqueue(Response.decode(chunk))",
      'console["debug"](chunk)',
    ],
  ])("rejects the named %s control", (_name, target, logging) => {
    expect(source.split(target)).toHaveLength(2);
    const control = source.replace(target, `${logging}; ${target}`);
    expect(() => inspectCodec(control)).toThrow(
      "Console reference in RPC payload codec",
    );
  });
});
