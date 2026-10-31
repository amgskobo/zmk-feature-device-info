import {
  act,
  fireEvent,
  render,
  screen,
  waitFor,
} from "@testing-library/react";
import {
  createConnectedMockZMKApp,
  ZMKAppProvider,
} from "@cormoran/zmk-studio-react-hook/testing";
import { call_rpc } from "@zmkfirmware/zmk-studio-ts-client";
import { DeviceInfoPanel, SUBSYSTEM_IDENTIFIER } from "../../web/src/App";
import { Response } from "../../web/src/proto/zmk/device_info/device_info";

jest.mock("@zmkfirmware/zmk-studio-ts-client", () => ({ call_rpc: jest.fn() }));

const reply = (board: string) => ({
  custom: {
    call: {
      payload: Response.encode(
        Response.create({ deviceInfo: { build: { board } } }),
      ).finish(),
    },
  },
});

it.each(["success", "error"])(
  "ignores an old connection's manual refresh %s after a new connection loads",
  async (outcome) => {
    const first = createConnectedMockZMKApp({
      subsystems: [SUBSYSTEM_IDENTIFIER],
    });
    const second = createConnectedMockZMKApp({
      subsystems: [SUBSYSTEM_IDENTIFIER],
    });
    expect(first.state.connection).not.toBe(second.state.connection);
    let finish!: (value: ReturnType<typeof reply>) => void;
    let reject!: (error: Error) => void;
    const pending = new Promise<ReturnType<typeof reply>>((resolve, fail) => {
      finish = resolve;
      reject = fail;
    });
    let firstCalls = 0;
    (call_rpc as jest.Mock).mockImplementation((connection) => {
      if (connection === first.state.connection) {
        return ++firstCalls === 1
          ? Promise.resolve(reply("first-board"))
          : pending;
      }
      return Promise.resolve(reply("second-board"));
    });
    const view = render(
      <ZMKAppProvider value={first}>
        <DeviceInfoPanel />
      </ZMKAppProvider>,
    );
    await screen.findByText("first-board");
    fireEvent.click(screen.getByRole("button", { name: "Refresh" }));
    await waitFor(() => expect(firstCalls).toBe(2));
    view.rerender(
      <ZMKAppProvider value={second}>
        <DeviceInfoPanel />
      </ZMKAppProvider>,
    );
    await screen.findByText("second-board");
    await act(async () => {
      if (outcome === "success") finish(reply("stale-first-board"));
      else reject(new Error("stale first refresh failed"));
    });
    expect(screen.getByText("second-board")).toBeInTheDocument();
    expect(screen.queryByText("stale-first-board")).not.toBeInTheDocument();
    expect(
      screen.queryByText("stale first refresh failed"),
    ).not.toBeInTheDocument();
  },
);

it("keeps a new manual refresh loading when the old refresh finishes", async () => {
  const first = createConnectedMockZMKApp({
    subsystems: [SUBSYSTEM_IDENTIFIER],
  });
  const second = createConnectedMockZMKApp({
    subsystems: [SUBSYSTEM_IDENTIFIER],
  });
  let finishFirst!: (value: ReturnType<typeof reply>) => void;
  let finishSecond!: (value: ReturnType<typeof reply>) => void;
  const pendingFirst = new Promise<ReturnType<typeof reply>>((resolve) => {
    finishFirst = resolve;
  });
  const pendingSecond = new Promise<ReturnType<typeof reply>>((resolve) => {
    finishSecond = resolve;
  });
  let firstCalls = 0;
  let secondCalls = 0;
  (call_rpc as jest.Mock).mockImplementation((connection) => {
    if (connection === first.state.connection) {
      return ++firstCalls === 1
        ? Promise.resolve(reply("first-board"))
        : pendingFirst;
    }
    return ++secondCalls === 1
      ? Promise.resolve(reply("second-board"))
      : pendingSecond;
  });
  const view = render(
    <ZMKAppProvider value={first}>
      <DeviceInfoPanel />
    </ZMKAppProvider>,
  );
  await screen.findByText("first-board");
  fireEvent.click(screen.getByRole("button", { name: "Refresh" }));
  await waitFor(() => expect(firstCalls).toBe(2));
  view.rerender(
    <ZMKAppProvider value={second}>
      <DeviceInfoPanel />
    </ZMKAppProvider>,
  );
  await screen.findByText("second-board");
  expect(screen.getByRole("button", { name: "Refresh" })).toBeEnabled();
  fireEvent.click(screen.getByRole("button", { name: "Refresh" }));
  await waitFor(() => expect(secondCalls).toBe(2));
  await act(async () => {
    finishFirst(reply("stale-first-board"));
  });
  expect(screen.getByRole("button", { name: "Loading..." })).toBeDisabled();
  expect(screen.queryByText("stale-first-board")).not.toBeInTheDocument();
  await act(async () => {
    finishSecond(reply("updated-second-board"));
  });
  expect(screen.getByText("updated-second-board")).toBeInTheDocument();
});

it("retires a pending manual refresh error when the panel unmounts", async () => {
  const first = createConnectedMockZMKApp({
    subsystems: [SUBSYSTEM_IDENTIFIER],
  });
  const second = createConnectedMockZMKApp({
    subsystems: [SUBSYSTEM_IDENTIFIER],
  });
  let reject!: (error: Error) => void;
  const pending = new Promise<ReturnType<typeof reply>>((_resolve, fail) => {
    reject = fail;
  });
  let firstCalls = 0;
  (call_rpc as jest.Mock).mockImplementation((connection) => {
    if (connection === first.state.connection) {
      return ++firstCalls === 1
        ? Promise.resolve(reply("first-board"))
        : pending;
    }
    return Promise.resolve(reply("second-board"));
  });
  const view = render(
    <ZMKAppProvider value={first}>
      <DeviceInfoPanel />
    </ZMKAppProvider>,
  );
  await screen.findByText("first-board");
  fireEvent.click(screen.getByRole("button", { name: "Refresh" }));
  await waitFor(() => expect(firstCalls).toBe(2));
  view.unmount();
  render(
    <ZMKAppProvider value={second}>
      <DeviceInfoPanel />
    </ZMKAppProvider>,
  );
  await screen.findByText("second-board");
  await act(async () => {
    reject(new Error("unmounted refresh failed"));
  });
  expect(screen.getByText("second-board")).toBeInTheDocument();
  expect(
    screen.queryByText("unmounted refresh failed"),
  ).not.toBeInTheDocument();
});

it.each(["success", "error"])(
  "retires a manual refresh %s when only the subsystem index changes",
  async (outcome) => {
    const app = createConnectedMockZMKApp({
      subsystems: [SUBSYSTEM_IDENTIFIER],
    });
    const connection = app.state.connection;
    let index = 0;
    app.findSubsystem = jest.fn(() => ({
      index,
      identifier: SUBSYSTEM_IDENTIFIER,
    }));
    let finish!: (value: ReturnType<typeof reply>) => void;
    let reject!: (error: Error) => void;
    const pending = new Promise<ReturnType<typeof reply>>((resolve, fail) => {
      finish = resolve;
      reject = fail;
    });
    let calls = 0;
    const indices: number[] = [];
    (call_rpc as jest.Mock).mockImplementation((_connection, request) => {
      indices.push(request.custom.call.subsystemIndex);
      if (request.custom.call.subsystemIndex === 1)
        return Promise.resolve(reply("new-index-board"));
      return ++calls === 1 ? Promise.resolve(reply("first-board")) : pending;
    });
    const view = render(
      <ZMKAppProvider value={app}>
        <DeviceInfoPanel />
      </ZMKAppProvider>,
    );
    await screen.findByText("first-board");
    fireEvent.click(screen.getByRole("button", { name: "Refresh" }));
    await waitFor(() => expect(calls).toBe(2));
    index = 1;
    view.rerender(
      <ZMKAppProvider value={app}>
        <DeviceInfoPanel />
      </ZMKAppProvider>,
    );
    await screen.findByText("new-index-board");
    expect(app.state.connection).toBe(connection);
    expect(indices).toEqual([0, 0, 1]);
    await act(async () => {
      if (outcome === "success") finish(reply("retired-index-board"));
      else reject(new Error("retired index refresh failed"));
    });
    expect(screen.getByText("new-index-board")).toBeInTheDocument();
    expect(screen.queryByText("retired-index-board")).not.toBeInTheDocument();
    expect(
      screen.queryByText("retired index refresh failed"),
    ).not.toBeInTheDocument();
  },
);

it.each(["success", "error"])(
  "preserves automatic-load cancellation for an old connection %s",
  async (outcome) => {
    const first = createConnectedMockZMKApp({
      subsystems: [SUBSYSTEM_IDENTIFIER],
    });
    const second = createConnectedMockZMKApp({
      subsystems: [SUBSYSTEM_IDENTIFIER],
    });
    let finish!: (value: ReturnType<typeof reply>) => void;
    let reject!: (error: Error) => void;
    const pending = new Promise<ReturnType<typeof reply>>((resolve, fail) => {
      finish = resolve;
      reject = fail;
    });
    let firstCalls = 0;
    (call_rpc as jest.Mock).mockImplementation((connection) => {
      if (connection === first.state.connection) {
        firstCalls++;
        return pending;
      }
      return Promise.resolve(reply("second-board"));
    });
    const view = render(
      <ZMKAppProvider value={first}>
        <DeviceInfoPanel />
      </ZMKAppProvider>,
    );
    await waitFor(() => expect(firstCalls).toBe(1));
    view.rerender(
      <ZMKAppProvider value={second}>
        <DeviceInfoPanel />
      </ZMKAppProvider>,
    );
    await screen.findByText("second-board");
    await act(async () => {
      if (outcome === "success") finish(reply("retired-auto-board"));
      else reject(new Error("retired automatic load failed"));
    });
    expect(screen.getByText("second-board")).toBeInTheDocument();
    expect(screen.queryByText("retired-auto-board")).not.toBeInTheDocument();
    expect(
      screen.queryByText("retired automatic load failed"),
    ).not.toBeInTheDocument();
  },
);
