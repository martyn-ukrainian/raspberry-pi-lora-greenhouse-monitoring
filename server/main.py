from fastapi import FastAPI

from database import init_db
from logger import get_logger, setup_logging
from measurements import router as measurements_router

setup_logging()
logger = get_logger(__name__)

init_db()

logger.info("App starting!")

app = FastAPI()
app.include_router(measurements_router)


@app.get("/")
def about() -> dict[str, str]:
    return {"service": "agro-server", "version": "0.1.0"}


@app.get("/health")
def health() -> dict[str, str]:
    return {"status": "ok"}
