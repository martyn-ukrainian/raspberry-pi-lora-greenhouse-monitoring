from sqlmodel import SQLModel, create_engine

from logger import get_logger
from settings import settings

logger = get_logger(__name__)

engine = create_engine(settings.database_url, connect_args={"check_same_thread": False})


def init_db() -> None:
    logger.info("Creating tables in %s", settings.database_url)
    SQLModel.metadata.create_all(engine)
