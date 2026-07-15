from fastapi import FastAPI
from fastapi.middleware.cors import CORSMiddleware

from alerts import router as alerts_router
from database import init_db
from greenhouses import router as greenhouses_router
from logger import get_logger, setup_logging
from measurements import router as measurements_router

setup_logging()
logger = get_logger(__name__)

init_db()

logger.info("App starting!")

app = FastAPI()

app.add_middleware(
    CORSMiddleware,
    allow_origins=["*"],
    allow_methods=["*"],
    allow_headers=["*"],
)

app.include_router(measurements_router)
app.include_router(greenhouses_router)
app.include_router(alerts_router)


@app.get("/")
def about() -> dict[str, str]:
    return {"service": "agro-server", "version": "0.1.0"}


@app.get("/health")
def health() -> dict[str, str]:
    return {"status": "ok"}
