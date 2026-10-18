-- Current Cached.exe primary opcode 6 (CSocket::DropItemPacket) database
-- slice.  The nine-input order is fixed by helper 0x00483dec. The compatible
-- 287 and Ertheia donors keep the same residence_guard insert body.

BEGIN;

CREATE SCHEMA IF NOT EXISTS "world";

CREATE TABLE IF NOT EXISTS "world"."residence_guard" (
    "residence_id" integer NOT NULL,
    "item_id" integer NOT NULL,
    "npc_id" integer NOT NULL,
    "guard_type" integer NOT NULL,
    "can_move" integer NOT NULL,
    "x" integer NOT NULL,
    "y" integer NOT NULL,
    "z" integer NOT NULL,
    "angle" integer NOT NULL,
    CONSTRAINT "pk_residence_guard" PRIMARY KEY ("x", "y", "z")
);

CREATE OR REPLACE FUNCTION "world"."lin_insertintoresidenceguard"(
    p_residence_id integer,
    p_item_id integer,
    p_npc_id integer,
    p_guard_type integer,
    p_can_move integer,
    p_x integer,
    p_y integer,
    p_z integer,
    p_angle integer
) RETURNS void
LANGUAGE sql
VOLATILE
AS $function$
    INSERT INTO "world"."residence_guard" (
        residence_id,
        item_id,
        npc_id,
        guard_type,
        can_move,
        x,
        y,
        z,
        angle
    ) VALUES (
        p_residence_id,
        p_item_id,
        p_npc_id,
        p_guard_type,
        p_can_move,
        p_x,
        p_y,
        p_z,
        p_angle
    );
$function$;

COMMIT;
