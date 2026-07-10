import logging

logging.basicConfig(
    level=logging.INFO,
    format="%(asctime)s [%(levelname)s %(name)s: %(message)s",
    datefmt="%Y-%m-%d %H:%M:%S",
)

logger = loggin.getLogger(__name__)

from fastapi import FastAPI

from database import init_db
from measurements import router as measurements_router

init_db()

app = FastAPI()
app.include_router(measurements_router)


@app.get("/")
def about() -> dict[str, str]:
    return {"service": "agro-server", "version": "0.1.0"}


@app.get("/health")
def health() -> dict[str, str]:
    return {"status": "ok"}
