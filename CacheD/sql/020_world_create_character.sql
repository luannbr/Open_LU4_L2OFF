-- Current Cached.exe primary opcode 2 (CreateCharacterPacket) database slice.
--
-- Procedure names, parameter counts, scalar families and Fetch requirements
-- are fixed by current CacheSocket.cpp 0x00485f60 and UserDb.cpp 0x005c2fd0.
-- The admitted relational body is identical in Classic 3.0, Ertheia and
-- modified-287. MSSQL's case-insensitive character comparisons are explicit
-- here because ordinary PostgreSQL text comparison is case-sensitive.

BEGIN;

CREATE SCHEMA IF NOT EXISTS "world";

-- The three broad families agree on both columns. Their text exports disagree
-- on the key, but both directly inspected report MDF catalogs carry the same
-- char_id primary key and keep this exact two-column shape. Together with
-- the current newly allocated character-ID path this selects one row per
-- character as a derived physical invariant.
CREATE TABLE "world"."user_pccafe_point" (
    "char_id" integer NOT NULL,
    "point" integer NOT NULL,
    CONSTRAINT "pk_user_pccafe_point" PRIMARY KEY ("char_id")
);

CREATE OR REPLACE FUNCTION "world"."lin_checkreserved"(
    p_char_name text,
    p_account_name text,
    p_account_id integer
) RETURNS TABLE (result integer)
LANGUAGE plpgsql
VOLATILE
AS $function$
DECLARE
    v_char_name text := rtrim(p_char_name);
    v_reserved_account_id integer;
BEGIN
    -- p_account_name is part of the current procedure ABI even though the
    -- surviving SQL body checks reservation ownership by account ID only.
    PERFORM p_account_name;

    SELECT reservation.account_id
    INTO v_reserved_account_id
    FROM "world"."user_name_reserved" AS reservation
    WHERE reservation.used = 0
      AND "world"."mssql_text_eq"(reservation.char_name, v_char_name)
    LIMIT 1;

    IF FOUND AND v_reserved_account_id <> p_account_id THEN
        RAISE EXCEPTION
            'Character name is reserved by other player: name = [%]',
            v_char_name;
    END IF;

    -- CUserDb binds one integer result but does not Fetch it.
    RETURN QUERY SELECT 0;
END;
$function$;

CREATE OR REPLACE FUNCTION "world"."lin_createchar"(
    p_char_name text,
    p_account_name text,
    p_account_id integer,
    p_pledge_id integer,
    p_builder integer,
    p_gender integer,
    p_race integer,
    p_class integer,
    p_world integer,
    p_xloc integer,
    p_yloc integer,
    p_zloc integer,
    p_hp double precision,
    p_mp double precision,
    p_sp bigint,
    p_exp bigint,
    p_level integer,
    p_alignment integer,
    p_pk integer,
    p_duel integer,
    p_pk_pardon integer,
    p_face_index integer,
    p_hair_shape_index integer,
    p_hair_color_index integer
) RETURNS TABLE (char_id integer)
LANGUAGE plpgsql
VOLATILE
AS $function$
DECLARE
    v_char_name text := rtrim(p_char_name);
    v_char_id integer := 0;
    v_reserved_name varchar(50);
    v_reserved_account_id integer;
    v_pccafe_now timestamp without time zone;
BEGIN
    -- Preserve the donor's Unicode LIKE literally. SQL Server RTRIM removes
    -- U+0020, while the N' ' pattern keeps its one significant space.
    IF v_char_name LIKE ' ' THEN
        RETURN QUERY SELECT 0;
        RETURN;
    END IF;

    IF EXISTS (
        SELECT 1
        FROM "world"."user_prohibit" AS prohibited
        WHERE "world"."mssql_text_eq"(prohibited.char_name, v_char_name)
    ) THEN
        RETURN QUERY SELECT 0;
        RETURN;
    END IF;

    IF EXISTS (
        SELECT 1
        FROM "world"."user_prohibit_word" AS prohibited
        WHERE position(lower(prohibited.words) IN lower(v_char_name)) > 0
    ) THEN
        RETURN QUERY SELECT 0;
        RETURN;
    END IF;

    SELECT reservation.char_name, reservation.account_id
    INTO v_reserved_name, v_reserved_account_id
    FROM "world"."user_name_reserved" AS reservation
    WHERE reservation.used = 0
      AND "world"."mssql_text_eq"(reservation.char_name, v_char_name)
    LIMIT 1;

    IF FOUND AND v_reserved_account_id <> p_account_id THEN
        RETURN QUERY SELECT 0;
        RETURN;
    END IF;

    INSERT INTO "world"."user_data" (
        char_name, account_name, account_id, pledge_id,
        builder, gender, race, class, subjob0_class,
        world, xloc, yloc, zloc,
        hp, mp, max_hp, max_mp, sp, exp, lev,
        align, pk, pkpardon, duel, create_date,
        face_index, hair_shape_index, hair_color_index
    ) VALUES (
        v_char_name, p_account_name, p_account_id, p_pledge_id,
        p_builder, p_gender, p_race, p_class, p_class,
        p_world, p_xloc, p_yloc, p_zloc,
        p_hp, p_mp, trunc(p_hp)::integer, trunc(p_mp)::integer,
        p_sp, p_exp, p_level,
        p_alignment, p_pk,
        -- This apparently reversed pair is the surviving procedure's exact
        -- column/value order: PKpardon receives @Duel, duel receives
        -- @PKPardon. Current opcode 2 supplies zero for both.
        p_duel, p_pk_pardon, "world"."mssql_getdate"(),
        p_face_index, p_hair_shape_index, p_hair_color_index
    ) RETURNING user_data.char_id INTO v_char_id;

    INSERT INTO "world"."user_slot" (char_id)
    VALUES (v_char_id);

    -- This expired ITGAME-2099 branch is present in every current-compatible
    -- broad-family lin_CreateChar body. Keep its inclusive local-time window
    -- and fixed legacy award even though it is normally unreachable today.
    v_pccafe_now := "world"."mssql_getdate"();
    IF v_pccafe_now >= TIMESTAMP '2010-11-10 06:00:00'
       AND v_pccafe_now <= TIMESTAMP '2010-11-24 06:00:00' THEN
        INSERT INTO "world"."user_pccafe_point" (char_id, point)
        VALUES (v_char_id, 20000);
    END IF;

    INSERT INTO "world"."user_history" (
        char_name, char_id, log_date, log_action, account_name, create_date
    ) VALUES (
        v_char_name, v_char_id, "world"."mssql_getdate"(), 1, p_account_name, NULL
    );

    IF v_reserved_name IS NOT NULL THEN
        UPDATE "world"."user_name_reserved" AS reservation
        SET used = 1
        WHERE "world"."mssql_text_eq"(
            reservation.char_name,
            v_reserved_name
        );
    END IF;

    INSERT INTO "world"."user_data_extra" (char_id, modified_date)
    VALUES (v_char_id, "world"."mssql_getdate"())
    ON CONFLICT ON CONSTRAINT "pk_user_data_extra" DO UPDATE
    SET modified_date = EXCLUDED.modified_date;

    RETURN QUERY SELECT v_char_id;
END;
$function$;

CREATE OR REPLACE FUNCTION "world"."lin_getbuilderaccount"(
    p_account_name text
) RETURNS TABLE (default_builder integer)
LANGUAGE sql
STABLE
AS $function$
    SELECT builder.default_builder
    FROM "world"."builder_account" AS builder
    WHERE "world"."mssql_text_eq"(builder.account_name, p_account_name)
    LIMIT 1;
$function$;

CREATE OR REPLACE FUNCTION "world"."lin_updateuserbeauty"(
    p_char_id integer,
    p_face_index_ex integer,
    p_hair_shape_index_ex integer,
    p_hair_color_index_ex integer
) RETURNS void
LANGUAGE sql
VOLATILE
AS $function$
    INSERT INTO "world"."user_beauty" (
        char_id, face_index_ex, hairshape_index_ex, haircolor_index_ex
    ) VALUES (
        p_char_id, p_face_index_ex, p_hair_shape_index_ex,
        p_hair_color_index_ex
    )
    ON CONFLICT (char_id) DO UPDATE
    SET face_index_ex = EXCLUDED.face_index_ex,
        hairshape_index_ex = EXCLUDED.hairshape_index_ex,
        haircolor_index_ex = EXCLUDED.haircolor_index_ex;
$function$;

CREATE OR REPLACE FUNCTION "world"."lin_savevotesystem"(
    p_char_id integer,
    p_bonus_time_limit integer,
    p_play_time integer,
    p_vote_count integer,
    p_bonus_count integer,
    p_play_time_count integer,
    p_last_init_time text
) RETURNS void
LANGUAGE sql
VOLATILE
AS $function$
    INSERT INTO "world"."user_vote_system" (
        char_id, bonustimelimit, playtime, votecount,
        bonuscount, playtimecount, lastinittime
    ) VALUES (
        p_char_id, p_bonus_time_limit, p_play_time, p_vote_count,
        p_bonus_count, p_play_time_count,
        p_last_init_time::timestamp without time zone
    )
    ON CONFLICT (char_id) DO UPDATE
    SET bonustimelimit = EXCLUDED.bonustimelimit,
        playtime = EXCLUDED.playtime,
        votecount = EXCLUDED.votecount,
        bonuscount = EXCLUDED.bonuscount,
        playtimecount = EXCLUDED.playtimecount,
        lastinittime = EXCLUDED.lastinittime;
$function$;

COMMIT;
