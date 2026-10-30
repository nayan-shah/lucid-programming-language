import { createApp } from "./app.js";

const port = Number(process.env.PORT ?? 4173);
const app = createApp();
const server = app.listen(port, "127.0.0.1", () => {
  console.log(`Lucid API listening at http://127.0.0.1:${port}`);
});

function shutdown() {
  server.close(() => {
    void app.locals.shutdown().then(
      () => process.exit(0),
      (error) => {
        console.error(`Unable to clean up interpreter sessions: ${error.message}`);
        process.exit(1);
      },
    );
  });
}

process.on("SIGINT", shutdown);
process.on("SIGTERM", shutdown);
