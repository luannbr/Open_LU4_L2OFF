-- PostgreSQL implementations of every stored-procedure call made by the
-- selected L2Authd.exe.
--
-- Parameter types and returned-column order are the current EXE's ODBC bind
-- contract. Bodies marked [DONOR] are direct PostgreSQL translations of the
-- lin2db.sql shipped beside the EXE/PDB (with the restored AionAccounts backup
-- used only where noted). Bodies marked [SEMANTIC] implement an observed
-- contract whose production SQL body was not included in the available
-- deployment. They are kept here, outside recovered C++.

BEGIN;
SET search_path TO authd, public;

-- Account load/authentication ------------------------------------------------

-- [DONOR] ap_GStat, extended only with the selected EXE's optional active flag.
CREATE OR REPLACE FUNCTION ap_gstat(p_account text)
RETURNS TABLE (
    out_uid integer,
    out_pay_stat integer,
    out_login_flag integer,
    out_warn_flag integer,
    out_block_flag integer,
    out_block_flag2 integer,
    out_subscription_flag integer,
    out_last_world smallint,
    out_block_end_date timestamp without time zone,
    out_forbidden_servers bytea,
    out_active_flag integer
)
LANGUAGE plpgsql
VOLATILE
AS $$
BEGIN
    SELECT
        ua.uid,
        ua.pay_stat::integer,
        ua.login_flag,
        ua.warn_flag,
        ua.block_flag,
        ua.block_flag2,
        ua.subscription_flag,
        ua.last_world::smallint,
        ua.block_end_date,
        ua.forbidden_servers,
        ua.active_flag
    INTO
        out_uid,
        out_pay_stat,
        out_login_flag,
        out_warn_flag,
        out_block_flag,
        out_block_flag2,
        out_subscription_flag,
        out_last_world,
        out_block_end_date,
        out_forbidden_servers,
        out_active_flag
    FROM authd.user_account AS ua
    WHERE ua.account = p_account;

    UPDATE authd.user_account
    SET last_login = localtimestamp
    WHERE account = p_account;

    -- SQL Server output parameters remain present even if the SELECT matched
    -- no row. A single all-NULL PostgreSQL row keeps that behavior.
    RETURN NEXT;
END;
$$;

-- [DONOR] ap_GPwdWithFlag. otp_flag is the optional fourth output selected by
-- checkTasStatusFromDb in the current executable.
CREATE OR REPLACE FUNCTION ap_gpwdwithflag(p_account text)
RETURNS TABLE (
    out_password bytea,
    out_encryption_flag smallint,
    out_telephone_auth_flag smallint
)
LANGUAGE plpgsql
STABLE
AS $$
BEGIN
    SELECT ua.password, ua.new_pwd_flag, ua.otp_flag
    INTO out_password, out_encryption_flag, out_telephone_auth_flag
    FROM authd.user_auth AS ua
    WHERE ua.account = p_account;
    RETURN NEXT;
END;
$$;

-- [EXE]/[SEMANTIC] The old donor is a three-literal stub. The main
-- PostgreSQL result has all three selected-image fields. PostgreSqlOdbc.cpp
-- projects the subset enabled by useSecurityCard/UserData.
CREATE OR REPLACE FUNCTION ap_gstatetc(p_account text)
RETURNS TABLE (
    security_card integer,
    opaque_user_data bytea,
    opaque_user_data_length smallint
)
LANGUAGE sql
STABLE
AS $$
    SELECT
        COALESCE(ae.security_card, 0),
        ae.user_data,
        COALESCE(ae.user_data_length, 0)::smallint
    FROM (VALUES (1)) AS one(dummy)
    LEFT JOIN authd.account_etc AS ae ON ae.account = p_account
$$;

CREATE OR REPLACE FUNCTION ap_gstatetct(p_account text)
RETURNS TABLE (
    security_card integer,
    opaque_user_data bytea,
    opaque_user_data_length smallint
)
LANGUAGE sql
STABLE
AS $$
    SELECT * FROM authd.ap_gstatetc(p_account)
$$;

-- [DONOR] The restored AionAccounts backup supplies the non-empty fallback:
-- both timestamps are the first day of the current year and info is 8 zeroes.
CREATE OR REPLACE FUNCTION ap_getaccountgameslot(p_uid integer)
RETURNS TABLE (
    out_last_logout timestamp without time zone,
    out_activate timestamp without time zone,
    out_information bytea
)
LANGUAGE sql
STABLE
AS $$
    SELECT
        COALESCE(ags.last_logout_time, date_trunc('year', localtimestamp)),
        COALESCE(ags.activate_time, date_trunc('year', localtimestamp)),
        COALESCE(
            ags.activate_information,
            decode(repeat('00', 8), 'hex'))
    FROM (VALUES (1)) AS one(dummy)
    LEFT JOIN authd.account_game_slot AS ags ON ags.uid = p_uid
$$;

-- [DONOR]
CREATE OR REPLACE FUNCTION ap_snewpwd(
    p_account text,
    p_password bytea,
    p_encryption_flag smallint)
RETURNS integer
LANGUAGE plpgsql
VOLATILE
AS $$
BEGIN
    UPDATE authd.user_auth
    SET password = p_password,
        new_pwd_flag = p_encryption_flag
    WHERE account = p_account;
    RETURN 0;
END;
$$;

-- Child/play schedule is hosted by the third configured DB environment.
-- [SEMANTIC] Its exact current signature is recovered. The deployment body is
-- represented by editable rows instead of a permissive hard-coded response.
CREATE OR REPLACE FUNCTION ap_getservicetimeschedule(
    p_portal_uid integer,
    p_game_id smallint)
RETURNS TABLE (out_result integer, out_schedule text)
LANGUAGE sql
STABLE
AS $$
    SELECT
        COALESCE(sts.result, 0),
        COALESCE(sts.schedule, repeat('F', 48))
    FROM (VALUES (1)) AS one(dummy)
    LEFT JOIN authd.service_time_schedule AS sts
      ON sts.portal_uid = p_portal_uid
     AND sts.game_id = p_game_id
$$;

-- Restrictions and account lookup -------------------------------------------

-- [DONOR]
CREATE OR REPLACE FUNCTION ap_getrestriction(p_uid integer)
RETURNS TABLE (reason integer, message text)
LANGUAGE sql
STABLE
AS $$
    SELECT bm.reason, bm.msg::text
    FROM authd.block_msg AS bm
    WHERE bm.uid = p_uid
$$;

-- [DONOR]
CREATE OR REPLACE FUNCTION ap_getgameaccountno(p_account text)
RETURNS TABLE (uid integer)
LANGUAGE sql
STABLE
AS $$
    SELECT ua.uid
    FROM authd.user_account AS ua
    WHERE ua.account = p_account
$$;

-- [EXE]/[SEMANTIC] The misspelling Restirction is part of the shipping ABI.
-- Return codes match the caller's switch: 0 success, 1 unknown uid, 2 already
-- restricted (also success to the caller), 3 persistence failure.
CREATE OR REPLACE FUNCTION ap_setaccountrestirction(
    p_uid integer,
    p_restriction_kind smallint)
RETURNS TABLE (
    procedure_result integer,
    out_account text,
    out_restriction_number integer
)
LANGUAGE plpgsql
VOLATILE
AS $$
DECLARE
    restriction bigint;
BEGIN
    SELECT ua.account
    INTO out_account
    FROM authd.user_account AS ua
    WHERE ua.uid = p_uid;

    IF out_account IS NULL THEN
        procedure_result := 1;
        out_restriction_number := 0;
        RETURN NEXT;
        RETURN;
    END IF;

    INSERT INTO authd.account_restriction (
        uid, account, restriction_kind, message)
    VALUES (
        p_uid, out_account, p_restriction_kind, NULL)
    RETURNING restriction_id INTO restriction;

    INSERT INTO authd.block_msg (uid, account, msg, reason)
    VALUES (p_uid, out_account, NULL, p_restriction_kind::integer);

    UPDATE authd.user_account
    SET block_flag = 1
    WHERE uid = p_uid;

    procedure_result := 0;
    out_restriction_number := restriction::integer;
    RETURN NEXT;
EXCEPTION WHEN OTHERS THEN
    procedure_result := 3;
    out_account := COALESCE(out_account, '');
    out_restriction_number := 0;
    RETURN NEXT;
END;
$$;

-- [DONOR signature plus current literal reason argument]
CREATE OR REPLACE FUNCTION ap_setillegallogintrace(
    p_uid integer,
    p_ip text,
    p_reason integer)
RETURNS integer
LANGUAGE plpgsql
VOLATILE
AS $$
BEGIN
    INSERT INTO authd.gm_illegal_login (account, ip)
    VALUES (p_uid::text, left(p_ip, 15));
    INSERT INTO authd.illegal_login_trace (uid, ip, reason)
    VALUES (p_uid, left(p_ip, 15), p_reason);
    RETURN 0;
END;
$$;

-- [DONOR]
CREATE OR REPLACE FUNCTION ap_setpasswordresetflag(p_uid integer)
RETURNS integer
LANGUAGE plpgsql
VOLATILE
AS $$
BEGIN
    UPDATE authd.user_account
    SET login_flag = login_flag | 1
    WHERE uid = p_uid;
    RETURN 0;
END;
$$;

-- Time and login persistence -------------------------------------------------

-- [DONOR]
CREATE OR REPLACE FUNCTION ap_gusertime(p_uid integer)
RETURNS TABLE (out_user_time integer)
LANGUAGE sql
STABLE
AS $$
    SELECT COALESCE(
        (SELECT ut.total_time FROM authd.user_time AS ut WHERE ut.uid = p_uid),
        0)
$$;

-- [EXE]/[SEMANTIC] No deployment body was found. next_time is the donor
-- user_time field matching most closely to the current duration query.
-- present_time/total_time are deterministic fallbacks for older rows.
CREATE OR REPLACE FUNCTION ap_guserduration(p_uid integer)
RETURNS TABLE (out_duration integer)
LANGUAGE sql
STABLE
AS $$
    SELECT COALESCE(
        (SELECT COALESCE(ut.next_time, ut.present_time, ut.total_time)
         FROM authd.user_time AS ut
         WHERE ut.uid = p_uid),
        0)
$$;

-- [DONOR]
CREATE OR REPLACE FUNCTION ap_susertime(
    p_use_time integer,
    p_uid integer)
RETURNS integer
LANGUAGE plpgsql
VOLATILE
AS $$
BEGIN
    UPDATE authd.user_time
    SET total_time = total_time - p_use_time,
        present_time = present_time - p_use_time,
        op_date = localtimestamp
    WHERE uid = p_uid;
    RETURN 0;
END;
$$;

-- [DONOR] Four-argument overload selected by personal-specific-date charging.
CREATE OR REPLACE FUNCTION ap_susertime(
    p_use_time integer,
    p_uid integer,
    p_pay_stat integer,
    p_login_time timestamp without time zone)
RETURNS integer
LANGUAGE plpgsql
VOLATILE
AS $$
DECLARE
    pay_type integer := (p_pay_stat % 1000) / 100;
BEGIN
    IF pay_type = 2 OR
       (pay_type = 5 AND EXISTS (
            SELECT 1
            FROM authd.user_pay AS up
            JOIN authd.user_account AS ua
              ON btrim(up.account) = ua.account
            WHERE ua.uid = p_uid
              AND up.start_date < p_login_time)) THEN
        UPDATE authd.user_time
        SET total_time = total_time - p_use_time,
            present_time = present_time - p_use_time,
            op_date = localtimestamp
        WHERE uid = p_uid;
    END IF;
    RETURN 0;
END;
$$;

-- [DONOR] ap_SLog. The slot timestamp write is [SEMANTIC] state needed by the
-- selected ap_GetAccountGameSlot output contract.
CREATE OR REPLACE FUNCTION ap_slog(
    p_uid integer,
    p_last_login timestamp without time zone,
    p_last_logout timestamp without time zone,
    p_last_game integer,
    p_last_world smallint,
    p_last_ip text)
RETURNS integer
LANGUAGE plpgsql
VOLATILE
AS $$
BEGIN
    UPDATE authd.user_account
    SET last_login = p_last_login,
        last_logout = p_last_logout,
        last_world = p_last_world,
        last_game = p_last_game,
        last_ip = left(p_last_ip, 15)
    WHERE uid = p_uid;

    INSERT INTO authd.account_game_slot (uid, last_logout_time)
    VALUES (p_uid, p_last_logout)
    ON CONFLICT (uid) DO UPDATE
    SET last_logout_time = EXCLUDED.last_logout_time;
    RETURN 0;
END;
$$;

-- Account game-slot/character state -----------------------------------------

-- [EXE]/[SEMANTIC]
CREATE OR REPLACE FUNCTION ap_registeraccountgameslot(
    p_uid integer,
    p_activate_time text,
    p_information bytea)
RETURNS TABLE (procedure_result integer)
LANGUAGE plpgsql
VOLATILE
AS $$
BEGIN
    INSERT INTO authd.account_game_slot (
        uid, activate_time, activate_information)
    VALUES (
        p_uid,
        p_activate_time::timestamp without time zone,
        p_information)
    ON CONFLICT (uid) DO UPDATE
    SET activate_time = EXCLUDED.activate_time,
        activate_information = EXCLUDED.activate_information;
    procedure_result := 0;
    RETURN NEXT;
EXCEPTION WHEN OTHERS THEN
    procedure_result := 1;
    RETURN NEXT;
END;
$$;

-- [EXE]/[SEMANTIC] The restored donor body is an explicit no-op. The current
-- EXE distinguishes normal and forced updates, so the portable body persists
-- both: normal updates never lower a level. Forced updates may do so.
CREATE OR REPLACE FUNCTION ap_registeraccountgamecharacter(
    p_uid integer,
    p_server_id smallint,
    p_character_id integer,
    p_level integer,
    p_updated_at text,
    p_force boolean)
RETURNS TABLE (procedure_result integer)
LANGUAGE plpgsql
VOLATILE
AS $$
BEGIN
    INSERT INTO authd.account_game_character (
        uid, server_id, character_id, level, updated_at)
    VALUES (
        p_uid,
        p_server_id,
        p_character_id,
        p_level,
        p_updated_at::timestamp without time zone)
    ON CONFLICT (uid, server_id, character_id) DO UPDATE
    SET level = CASE
            WHEN p_force THEN EXCLUDED.level
            ELSE GREATEST(authd.account_game_character.level, EXCLUDED.level)
        END,
        updated_at = EXCLUDED.updated_at;
    procedure_result := 0;
    RETURN NEXT;
EXCEPTION WHEN OTHERS THEN
    procedure_result := 1;
    RETURN NEXT;
END;
$$;

CREATE OR REPLACE FUNCTION ap_deleteaccountgamecharacter(
    p_uid integer,
    p_server_id smallint,
    p_character_id integer)
RETURNS TABLE (procedure_result integer)
LANGUAGE plpgsql
VOLATILE
AS $$
BEGIN
    DELETE FROM authd.account_game_character
    WHERE uid = p_uid
      AND server_id = p_server_id
      AND character_id = p_character_id;
    procedure_result := 0;
    RETURN NEXT;
END;
$$;

CREATE OR REPLACE FUNCTION ap_getaccountmaxlevelcharacter(p_uid integer)
RETURNS TABLE (
    out_server_id smallint,
    out_character_id integer,
    out_level integer,
    procedure_result integer)
LANGUAGE plpgsql
STABLE
AS $$
BEGIN
    SELECT agc.server_id, agc.character_id, agc.level
    INTO out_server_id, out_character_id, out_level
    FROM authd.account_game_character AS agc
    WHERE agc.uid = p_uid
    ORDER BY agc.level DESC, agc.updated_at DESC, agc.character_id
    LIMIT 1;

    IF NOT FOUND THEN
        out_server_id := 0;
        out_character_id := 0;
        out_level := 0;
        procedure_result := 1;
    ELSE
        procedure_result := 0;
    END IF;
    RETURN NEXT;
END;
$$;

CREATE OR REPLACE FUNCTION ap_getaccountgamecharacter(
    p_uid integer,
    p_server_id smallint)
RETURNS TABLE (
    out_character_id integer,
    out_level integer,
    procedure_result integer)
LANGUAGE plpgsql
STABLE
AS $$
BEGIN
    SELECT agc.character_id, agc.level
    INTO out_character_id, out_level
    FROM authd.account_game_character AS agc
    WHERE agc.uid = p_uid
      AND agc.server_id = p_server_id
    ORDER BY agc.level DESC, agc.updated_at DESC, agc.character_id
    LIMIT 1;

    IF NOT FOUND THEN
        out_character_id := 0;
        out_level := 0;
        procedure_result := 1;
    ELSE
        procedure_result := 0;
    END IF;
    RETURN NEXT;
END;
$$;

-- Older active DelChar path. Signature is present in the EXE.
CREATE OR REPLACE FUNCTION ap_delchar(
    p_uid integer,
    p_character_id integer,
    p_server_id smallint)
RETURNS integer
LANGUAGE plpgsql
VOLATILE
AS $$
BEGIN
    DELETE FROM authd.account_game_character
    WHERE uid = p_uid
      AND character_id = p_character_id
      AND server_id = p_server_id;
    RETURN 0;
END;
$$;

-- Opaque user-data packet ----------------------------------------------------

-- [EXE]/[SEMANTIC]. The paired donor's body returns a literal 9 and stores
-- nothing. The current server immediately reads this payload back, so the
-- portable DB persists the exact length and bytes.
CREATE OR REPLACE FUNCTION ap_suserdata(
    p_account text,
    p_length smallint,
    p_payload bytea)
RETURNS integer
LANGUAGE plpgsql
VOLATILE
AS $$
BEGIN
    INSERT INTO authd.account_etc (
        account, user_data, user_data_length)
    VALUES (
        left(p_account, 14), p_payload, p_length)
    ON CONFLICT (account) DO UPDATE
    SET user_data = EXCLUDED.user_data,
        user_data_length = EXCLUDED.user_data_length;
    RETURN 9;
END;
$$;

-- Fatigue --------------------------------------------------------------------

-- [EXE]/[SEMANTIC]
CREATE OR REPLACE FUNCTION ap_sfatigueplaytime(
    p_uid integer,
    p_time integer)
RETURNS integer
LANGUAGE plpgsql
VOLATILE
AS $$
BEGIN
    INSERT INTO authd.fatigue_account (
        uid, play_time, last_logout_time)
    VALUES (
        p_uid,
        GREATEST(p_time, 0),
        extract(epoch FROM clock_timestamp())::integer)
    ON CONFLICT (uid) DO UPDATE
    SET play_time = authd.fatigue_account.play_time + GREATEST(p_time, 0),
        last_logout_time = EXCLUDED.last_logout_time,
        updated_at = localtimestamp;
    RETURN 0;
END;
$$;

-- [EXE]/[SEMANTIC] Inputs 4 and 5 are INOUT in the selected ODBC sequence.
-- The body combines stored and currently active play and applies deterministic
-- time-based recovery. This is isolated here because the production SQL body
-- was not included with the binary.
CREATE OR REPLACE FUNCTION ap_sfatigueresttime(
    p_fcid integer,
    p_account_count integer,
    p_recovery_time integer,
    p_active_play_time integer,
    p_current_rest_time integer)
RETURNS TABLE (
    out_play_time integer,
    out_rest_time integer)
LANGUAGE plpgsql
VOLATILE
AS $$
DECLARE
    stored_total integer := 0;
    stored_logout integer := 0;
    elapsed integer := 0;
    recovered integer := 0;
BEGIN
    SELECT fc.total_time, fc.last_logout_time
    INTO stored_total, stored_logout
    FROM authd.fatigue_customer AS fc
    WHERE fc.fcid = p_fcid;

    IF NOT FOUND THEN
        stored_total := 0;
        stored_logout := extract(epoch FROM clock_timestamp())::integer;
    END IF;

    elapsed := GREATEST(
        extract(epoch FROM clock_timestamp())::integer - stored_logout,
        0);
    recovered := CASE
        WHEN p_recovery_time > 0
            THEN elapsed / p_recovery_time
        ELSE 0
    END;
    out_play_time := GREATEST(
        stored_total + GREATEST(p_active_play_time, 0) - recovered,
        0);
    out_rest_time := GREATEST(p_current_rest_time, recovered);

    INSERT INTO authd.fatigue_customer (
        fcid, total_time, last_logout_time)
    VALUES (
        p_fcid, out_play_time,
        extract(epoch FROM clock_timestamp())::integer)
    ON CONFLICT (fcid) DO UPDATE
    SET total_time = EXCLUDED.total_time,
        last_logout_time = EXCLUDED.last_logout_time,
        updated_at = localtimestamp;

    -- Preserve the observed account-count input as an execution fact without
    -- inventing an otherwise unobserved column or C++ state owner.
    IF p_account_count < 0 THEN
        RAISE EXCEPTION 'negative fatigue account count';
    END IF;
    RETURN NEXT;
END;
$$;

CREATE OR REPLACE FUNCTION ap_gfatiguelastlogouttime(p_uid integer)
RETURNS TABLE (out_seconds integer)
LANGUAGE sql
STABLE
AS $$
    SELECT GREATEST(
        extract(epoch FROM clock_timestamp())::integer - COALESCE(
            (SELECT fc.last_logout_time
             FROM authd.fatigue_customer AS fc
             WHERE fc.fcid = p_uid),
            (SELECT fa.last_logout_time
             FROM authd.fatigue_account AS fa
             WHERE fa.uid = p_uid),
            extract(epoch FROM clock_timestamp())::integer),
        0)
$$;

-- Zone fatigue ---------------------------------------------------------------

CREATE OR REPLACE FUNCTION ap_szonefatiguereset(
    p_zone smallint,
    p_capsule_count smallint,
    p_capsule_ids bytea,
    p_capsule_amounts bytea)
RETURNS integer
LANGUAGE plpgsql
VOLATILE
AS $$
BEGIN
    IF p_zone < 0 OR p_zone >= 4 OR
       p_capsule_count < 0 OR p_capsule_count > 128 OR
       octet_length(p_capsule_ids) <> p_capsule_count OR
       octet_length(p_capsule_amounts) <> p_capsule_count * 4 THEN
        RETURN 1;
    END IF;

    INSERT INTO authd.zone_fatigue_configuration (
        zone, capsule_count, capsule_ids, capsule_time_amounts, reset_at)
    VALUES (
        p_zone, p_capsule_count, p_capsule_ids, p_capsule_amounts,
        localtimestamp)
    ON CONFLICT (zone) DO UPDATE
    SET capsule_count = EXCLUDED.capsule_count,
        capsule_ids = EXCLUDED.capsule_ids,
        capsule_time_amounts = EXCLUDED.capsule_time_amounts,
        reset_at = EXCLUDED.reset_at;

    UPDATE authd.zone_fatigue_state
    SET capsule_used_flags = decode(repeat('00', p_capsule_count), 'hex'),
        recent_reset_at = localtimestamp
    WHERE zone = p_zone;
    RETURN 0;
END;
$$;

CREATE OR REPLACE FUNCTION ap_szonefatigueremaintime(
    p_account_id integer,
    p_zone smallint,
    p_remain_time integer)
RETURNS integer
LANGUAGE plpgsql
VOLATILE
AS $$
DECLARE
    count_value smallint;
    reset_value timestamp without time zone;
BEGIN
    SELECT zfc.capsule_count, zfc.reset_at
    INTO count_value, reset_value
    FROM authd.zone_fatigue_configuration AS zfc
    WHERE zfc.zone = p_zone;
    IF NOT FOUND THEN
        RETURN 1;
    END IF;

    INSERT INTO authd.zone_fatigue_state (
        account_id, zone, remain_time, capsule_used_flags, recent_reset_at)
    VALUES (
        p_account_id,
        p_zone,
        p_remain_time,
        decode(repeat('00', count_value), 'hex'),
        reset_value)
    ON CONFLICT (account_id, zone) DO UPDATE
    SET remain_time = EXCLUDED.remain_time;
    RETURN 0;
END;
$$;

CREATE OR REPLACE FUNCTION ap_szonefatiguecapsuleuse(
    p_account_id integer,
    p_zone smallint,
    p_remain_time integer,
    p_capsule_id smallint,
    p_used_flags bytea)
RETURNS integer
LANGUAGE plpgsql
VOLATILE
AS $$
DECLARE
    count_value smallint;
    ids bytea;
    reset_value timestamp without time zone;
    index_value integer;
    found_capsule boolean := false;
BEGIN
    SELECT zfc.capsule_count, zfc.capsule_ids, zfc.reset_at
    INTO count_value, ids, reset_value
    FROM authd.zone_fatigue_configuration AS zfc
    WHERE zfc.zone = p_zone;
    IF NOT FOUND THEN
        RETURN 1;
    END IF;
    IF octet_length(p_used_flags) <> count_value THEN
        RETURN 2;
    END IF;
    FOR index_value IN 0 .. count_value - 1 LOOP
        IF get_byte(ids, index_value) = p_capsule_id THEN
            found_capsule := true;
            EXIT;
        END IF;
    END LOOP;
    IF NOT found_capsule THEN
        RETURN 3;
    END IF;

    INSERT INTO authd.zone_fatigue_state (
        account_id, zone, remain_time, capsule_used_flags,
        capsule_last_used_at, recent_reset_at)
    VALUES (
        p_account_id, p_zone, p_remain_time, p_used_flags,
        localtimestamp, reset_value)
    ON CONFLICT (account_id, zone) DO UPDATE
    SET remain_time = EXCLUDED.remain_time,
        capsule_used_flags = EXCLUDED.capsule_used_flags,
        capsule_last_used_at = EXCLUDED.capsule_last_used_at;
    RETURN 0;
END;
$$;

CREATE OR REPLACE FUNCTION ap_gzonefatigue(
    p_account_id integer,
    p_zone smallint)
RETURNS TABLE (
    procedure_result integer,
    out_remain_time integer,
    out_capsule_count smallint,
    out_used_flags bytea,
    out_capsule_ids bytea,
    out_capsule_amounts bytea,
    out_last_used timestamp without time zone,
    out_recent_reset timestamp without time zone
)
LANGUAGE plpgsql
VOLATILE
AS $$
DECLARE
    config_row authd.zone_fatigue_configuration%ROWTYPE;
    state_row authd.zone_fatigue_state%ROWTYPE;
BEGIN
    SELECT * INTO config_row
    FROM authd.zone_fatigue_configuration
    WHERE zone = p_zone;
    IF NOT FOUND THEN
        procedure_result := 5;
        out_remain_time := 0;
        out_capsule_count := 0;
        out_used_flags := ''::bytea;
        out_capsule_ids := ''::bytea;
        out_capsule_amounts := ''::bytea;
        out_last_used := NULL;
        out_recent_reset := NULL;
        RETURN NEXT;
        RETURN;
    END IF;

    SELECT * INTO state_row
    FROM authd.zone_fatigue_state
    WHERE account_id = p_account_id AND zone = p_zone;
    IF NOT FOUND THEN
        INSERT INTO authd.zone_fatigue_state (
            account_id, zone, remain_time, capsule_used_flags,
            recent_reset_at)
        VALUES (
            p_account_id,
            p_zone,
            0,
            decode(repeat('00', config_row.capsule_count), 'hex'),
            config_row.reset_at)
        RETURNING * INTO state_row;
    END IF;

    procedure_result := 0;
    out_remain_time := state_row.remain_time;
    out_capsule_count := config_row.capsule_count;
    out_used_flags := state_row.capsule_used_flags;
    IF octet_length(out_used_flags) < out_capsule_count THEN
        out_used_flags := out_used_flags || decode(
            repeat('00', out_capsule_count - octet_length(out_used_flags)),
            'hex');
    END IF;
    out_capsule_ids := config_row.capsule_ids;
    out_capsule_amounts := config_row.capsule_time_amounts;
    out_last_used := state_row.capsule_last_used_at;
    out_recent_reset := state_row.recent_reset_at;
    RETURN NEXT;
END;
$$;

-- Server list/status/statistics ----------------------------------------------

-- [EXE] Exact ten-column selected-image contract. The paired donor returns
-- fourteen columns and is therefore an older deployment shape.
CREATE OR REPLACE FUNCTION ap_getservers()
RETURNS TABLE (
    server_id smallint,
    server_name text,
    outer_ip text,
    inner_ip text,
    age_limit smallint,
    pk_flag smallint,
    server_kind integer,
    server_port integer,
    server_region smallint,
    domain_name text
)
LANGUAGE sql
STABLE
AS $$
    SELECT
        s.id,
        s.name::text,
        s.ip::text,
        s.inner_ip::text,
        s.age_limit,
        s.pk_flag,
        s.kind,
        s.port,
        s.region,
        s.domain_name::text
    FROM authd.server AS s
    ORDER BY s.id
$$;

CREATE OR REPLACE FUNCTION set_server_status_impl(
    p_server_id integer,
    p_status integer,
    p_test boolean)
RETURNS integer
LANGUAGE plpgsql
VOLATILE
AS $$
DECLARE
    server_name text;
BEGIN
    SELECT COALESCE(s.name, 'Unknown')
    INTO server_name
    FROM authd.server AS s
    WHERE s.id = p_server_id;
    server_name := COALESCE(server_name, 'Unknown');

    IF p_test THEN
        IF EXISTS (SELECT 1 FROM authd.worldstatus_test WHERE idx = p_server_id) THEN
            IF p_server_id = 0 THEN
                UPDATE authd.worldstatus_test AS ws
                SET status = p_status::smallint
                WHERE ws.idx IN (
                    SELECT s.id FROM authd.server AS s WHERE (s.kind & 4) = 0);
            ELSE
                UPDATE authd.worldstatus_test
                SET status = p_status::smallint
                WHERE idx = p_server_id;
            END IF;
        ELSE
            INSERT INTO authd.worldstatus_test (idx, server, status)
            VALUES (p_server_id, server_name, p_status::smallint);
        END IF;
    ELSE
        -- [DONOR] exact ap_SetServerStatus branching.
        IF EXISTS (SELECT 1 FROM authd.worldstatus WHERE idx = p_server_id) THEN
            IF p_server_id = 0 THEN
                UPDATE authd.worldstatus AS ws
                SET status = p_status::smallint
                WHERE ws.idx IN (
                    SELECT s.id FROM authd.server AS s WHERE (s.kind & 4) = 0);
            ELSE
                UPDATE authd.worldstatus
                SET status = p_status::smallint
                WHERE idx = p_server_id;
            END IF;
        ELSE
            INSERT INTO authd.worldstatus (idx, server, status)
            VALUES (p_server_id, server_name, p_status::smallint);
        END IF;
    END IF;
    RETURN 0;
END;
$$;

-- Overloads match the literal and bound casts used by the adapter.
CREATE OR REPLACE FUNCTION ap_setserverstatus(
    p_server_id integer, p_status integer)
RETURNS integer LANGUAGE sql VOLATILE
AS $$ SELECT authd.set_server_status_impl(p_server_id, p_status, false) $$;

CREATE OR REPLACE FUNCTION ap_setserverstatus(
    p_server_id smallint, p_status integer)
RETURNS integer LANGUAGE sql VOLATILE
AS $$ SELECT authd.set_server_status_impl(p_server_id, p_status, false) $$;

CREATE OR REPLACE FUNCTION ap_setserverstatus(
    p_server_id smallint, p_status smallint)
RETURNS integer LANGUAGE sql VOLATILE
AS $$ SELECT authd.set_server_status_impl(p_server_id, p_status, false) $$;

CREATE OR REPLACE FUNCTION ap_setserverstatust(
    p_server_id integer, p_status integer)
RETURNS integer LANGUAGE sql VOLATILE
AS $$ SELECT authd.set_server_status_impl(p_server_id, p_status, true) $$;

CREATE OR REPLACE FUNCTION ap_setserverstatust(
    p_server_id smallint, p_status integer)
RETURNS integer LANGUAGE sql VOLATILE
AS $$ SELECT authd.set_server_status_impl(p_server_id, p_status, true) $$;

CREATE OR REPLACE FUNCTION ap_setserverstatust(
    p_server_id smallint, p_status smallint)
RETURNS integer LANGUAGE sql VOLATILE
AS $$ SELECT authd.set_server_status_impl(p_server_id, p_status, true) $$;

-- [DONOR]
CREATE OR REPLACE FUNCTION ap_setconcurrentuserstatistics(
    p_server_no integer,
    p_world_users integer,
    p_user_limit integer,
    p_auth_users integer,
    p_wait_users integer)
RETURNS integer
LANGUAGE plpgsql
VOLATILE
AS $$
BEGIN
    INSERT INTO authd.user_count (
        server_id, world_user, limit_user, auth_user, wait_user)
    VALUES (
        p_server_no::smallint,
        p_world_users,
        p_user_limit,
        p_auth_users,
        p_wait_users);
    RETURN 0;
END;
$$;

COMMIT;
