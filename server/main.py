from fastapi import FastAPI
from fastapi.middleware.cors import CORSMiddleware
from fastapi.responses import FileResponse
from fastapi.staticfiles import StaticFiles

from alerts import router as alerts_router
from database import init_db
from events import router as events_router
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
app.include_router(events_router)

# Живий монітор — статична сторінка, яку сервер віддає сам. Same-origin,
# тож жодних CORS/адресних питань: у мережі теплиці відкриваєш <PI>:порт/live.
# Каталог static/ містить один self-contained index.html без зовнішніх
# ресурсів (PI може бути без інтернету).
app.mount("/live", StaticFiles(directory="static", html=True), name="live")


# Порівняння вузлів етапу 3 (опорний проти сну) — окрема сторінка того самого
# сервера, читає лише /measurements/aggregate і /measurements/latest.
@app.get("/stage3")
def stage3_page() -> FileResponse:
    return FileResponse("static/stage3.html")


@app.get("/")
def about() -> dict[str, str]:
    return {"service": "agro-server", "version": "0.1.0"}


@app.get("/health")
def health() -> dict[str, str]:
    return {"status": "ok"}
