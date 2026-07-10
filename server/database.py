from sqlmodel import SQLModel, create_engine

sqlite_url = "sqlite:///agro.db"
engine = create_engine(sqlite_url, connect_args={"check_same_thread": False})


def init_db() -> None:
    SQLModel.metadata.create_all(engine)
