from sqlmodel import SQLModel, create_engine

from logger import get_logger

logger = get_logger(__name__)

sqlite_url = "sqlite:///agro.db"
engine = create_engine(sqlite_url, connect_args={"check_same_thread": False})


def init_db() -> None:
    logger.info("Creating tables in %s", sqlite_url)
    SQLModel.metadata.create_all(engine)
